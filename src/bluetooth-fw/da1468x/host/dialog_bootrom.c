/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "board/board.h"
#include "drivers/accessory.h"
#include "drivers/gpio.h"
#include "drivers/periph_config.h"
#include "drivers/uart.h"
#include "kernel/util/delay.h"
#include "resource/resource.h"
#include "resource/resource_ids.auto.h"
#include "system/logging.h"
#include "system/passert.h"
#include "util/math.h"

#include <inttypes.h>
#include <stdint.h>

// The dialog BLE chip has a baked in bootloader capable of talking serial
// protocols. The bootloader allows a contiguous memory region to be loaded
// into RAM on the dialog chip. Early revisions of the DA1468x only support
// UART so we use that interface to accomodate this limitation. For more info
// on the interface itself see:
//
// http://support.dialog-semiconductor.com/resource/b-001-da14580-booting-serial-interfaces

#define DIALOG_BOOTROM_UART_BAUDRATE 57600 // Baudrate support baked into Dialog bootROM
#define DIALOG_BOOTROM_MAX_IMAGE_LOAD_SIZE UINT16_MAX

// Empirically, it seems to take 24ms for the revAD silicon to come out of
// reset and send the start message. This is way longer than its going to
// take to get a response to other bootROM messages so let's assume that's
// the max worst case wait time. To account for some variance, let's wait
// 100ms to play it safe.
#define DIALOG_BOOTROM_DEFAULT_CHAR_TIMEOUT_MS (100)
// For the revAE silicon, usually it takes ~220ms from deasserting RST to getting the
// first chip ID character over the UART RX, but I've observed it taking ~400ms at other times.
#define DIALOG_BOOTROM_FIRST_CHAR_TIMEOUT_MS (600)

// Some platform use two different UART peripherals! This means things need
// to be initialized and torn down seperately!
static bool prv_platform_uses_two_uart_peripherals(void) {
  return (BT_RX_BOOTROM_UART != BT_TX_BOOTROM_UART);
}

static void prv_init_dialog_bootrom_interface(void) {
  periph_config_acquire_lock();

  if (prv_platform_uses_two_uart_peripherals()) {
    uart_init_rx_only(BT_RX_BOOTROM_UART);
    uart_init_tx_only(BT_TX_BOOTROM_UART);
    uart_set_baud_rate(BT_RX_BOOTROM_UART, DIALOG_BOOTROM_UART_BAUDRATE);
    uart_set_baud_rate(BT_TX_BOOTROM_UART, DIALOG_BOOTROM_UART_BAUDRATE);
  } else {
    uart_init(BT_RX_BOOTROM_UART);
    uart_set_baud_rate(BT_RX_BOOTROM_UART, DIALOG_BOOTROM_UART_BAUDRATE);
  }

  periph_config_release_lock();
}

static void prv_deinit_dialog_bootrom_interface(void) {
  periph_config_acquire_lock();
  if (prv_platform_uses_two_uart_peripherals()) {
    uart_deinit(BT_RX_BOOTROM_UART);
    uart_deinit(BT_TX_BOOTROM_UART);
  } else {
    uart_deinit(BT_RX_BOOTROM_UART);
  }
  periph_config_release_lock();
}

static void prv_setup_dialog_reset_gpio(void) {
  periph_config_acquire_lock();

  // configure the GPIO for RST:
  gpio_output_init(&BOARD_CONFIG_BT_COMMON.reset, GPIO_OType_PP, GPIO_Speed_25MHz);

  periph_config_release_lock();
}

static void prv_enter_reset(void) {
  gpio_output_set(&BOARD_CONFIG_BT_COMMON.reset, true);
  delay_us(100);
}

static void prv_exit_reset(void) {
  gpio_output_set(&BOARD_CONFIG_BT_COMMON.reset, false);
}

static void prv_send_char(uint8_t c) {
  uart_write_byte(BT_TX_BOOTROM_UART, c);
  uart_wait_for_tx_complete(BT_TX_BOOTROM_UART);
}

static bool prv_bootrom_wait_for_char_with_timeout(char *char_received, uint32_t wait_time_ticks) {
  const uint32_t end_ticks = rtc_get_ticks() + wait_time_ticks;

  do {
    UARTRXErrorFlags errors = uart_has_errored_out(BT_RX_BOOTROM_UART);
    if (errors.error_mask != 0) {
      uint16_t dr = uart_read_byte(BT_RX_BOOTROM_UART);

      // The Dialog bootROM attempts to talk several serial protocols. While doing this it will
      // reconfigure gpios as inputs and outputs and change what data it is pushing on the lines.
      // For some architectures, this can result in framing errors getting detected. Therefore, if
      // it's a frame error, let's carry on and see if the data we read matches what we expect
      // anyway
      if (!errors.framing_error) {
        PBL_LOG(LOG_LEVEL_WARNING, "UART ERROR: 0x%x 0x%d", (int)errors.error_mask, (int)dr);
        return false;
      } else {
        PBL_LOG(LOG_LEVEL_DEBUG, "Dialog bootrom framing error, continuing onward");
      }
    }

    if (uart_is_rx_ready(BT_RX_BOOTROM_UART)) {
      *char_received = uart_read_byte(BT_RX_BOOTROM_UART);
      return true;
    }
  } while (rtc_get_ticks() <= end_ticks);

  PBL_LOG(LOG_LEVEL_ERROR, "Timed out waiting for char");
  return false;
}

static bool prv_bootrom_wait_for_char(char *char_received) {
  const uint32_t wait_time_ticks = (DIALOG_BOOTROM_DEFAULT_CHAR_TIMEOUT_MS * RTC_TICKS_HZ) / 1000;
  return prv_bootrom_wait_for_char_with_timeout(char_received, wait_time_ticks);
}

static bool prv_download_patch_from_system_resources(uint16_t bootloader_size) {
  uint8_t running_xor = 0;
  uint32_t curr_offset = 0;

  while (curr_offset < bootloader_size) {
    const size_t bytes_left = bootloader_size - curr_offset;

    uint8_t data_buf[256] = { 0 };
    const size_t read_len = MIN(bytes_left, sizeof(data_buf));

    if (!resource_load_byte_range_system(SYSTEM_APP, RESOURCE_ID_BT_BOOT_IMAGE, curr_offset,
                                         data_buf, read_len)) {
      PBL_LOG(LOG_LEVEL_ERROR, "Failed to load patch from system resources");
      return false;
    }

    for (uint32_t i = 0; i < read_len; i++) {
      running_xor ^= data_buf[i];
      prv_send_char(data_buf[i]);
    }

    curr_offset += read_len;
  }

  // Note: As soon as we send the last byte, the bootROM will send the crc which is
  // simply an xor of the bytes transmitted

  char c;
  if (!prv_bootrom_wait_for_char(&c)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error waiting for CRC response");
    return false;
  }
  if (c == running_xor) {
    prv_send_char(0x06);
    return true;
  }

  PBL_LOG(LOG_LEVEL_ERROR, "CRC mismatch! expected=0x%"PRIx8" vs recv'd=0x%"PRIx8, running_xor, c);
  return false;
}

static bool prv_send_header(uint16_t bootloader_size) {
  // Send the header
  prv_send_char(0x01); // Start of Header: 0x01
  prv_send_char(bootloader_size & 0xff); // LEN_LSB
  prv_send_char((bootloader_size >> 8) & 0xff); // LEN_MSB

  char c;
  if (!prv_bootrom_wait_for_char(&c)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error waiting for header response");
    return false;
  }
  if (c != 0x06) {
    PBL_LOG(LOG_LEVEL_ERROR, "Bootrom did not accept header (0x%x)", c);
    return false;
  }
  return true;
}

static bool prv_load_second_stage_bootloader(void) {
  bool success = true;

  // Find the image we are going to be downloading
  uint32_t bootloader_size = resource_size(SYSTEM_APP, RESOURCE_ID_BT_BOOT_IMAGE);
  PBL_ASSERTN(bootloader_size < DIALOG_BOOTROM_MAX_IMAGE_LOAD_SIZE);

  prv_setup_dialog_reset_gpio();

  // we don't know what kind of state the DA1468x is in so put the chip in reset
  // before we start listening on the UART for messages
  prv_enter_reset();

#if ACCESSORY_UART_IS_SHARED_WITH_BT
  // We are sharing a UART with the accessory port, so we need to block it from being used while we
  // are using it and then unblock at the end.
  accessory_block();
#endif

  // Configure GPIOs & peripherals
  prv_init_dialog_bootrom_interface();

  // UART is configured and listening for the start message, so exit reset
  prv_exit_reset();

  // Read until 0x02 start message is found:
  // Rev "AD" and before do not send out their chip ID before the 0x02 start message.
  // Rev "AE" and later send out their chip ID *twice* ("DA14681-01" or "DA14681AE \n\r")
  // before the 0x02 start message.
  char c;
  // 32 chars is more than needed, but let's add a bit of slack in case they extend it.
  const uint32_t max_chip_id_len = 32;
  uint32_t wait_time_ms = DIALOG_BOOTROM_FIRST_CHAR_TIMEOUT_MS;
  for (uint32_t i = 0; i < max_chip_id_len; ++i) {
    const uint32_t wait_time_ticks = (wait_time_ms * RTC_TICKS_HZ) / 1000;
    if (!prv_bootrom_wait_for_char_with_timeout(&c, wait_time_ticks)) {
      PBL_LOG(LOG_LEVEL_ERROR, "Error waiting for chip ID / start message (i=%"PRIu32")", i);
      success = false;
      goto cleanup;
    }
    if (c == 0x02) {
      // Got start message!
      break;
    } else if (c == 'D') { // First valid character we expect to receive for AE silicon
      wait_time_ms = DIALOG_BOOTROM_DEFAULT_CHAR_TIMEOUT_MS;
    }
  }

  if (c != 0x02) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unexpected start message: 0x%02x", c);
    success = false;
    goto cleanup;
  }

  // Got the start message so send the header
  if (!prv_send_header(bootloader_size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to send header");
    success = false;
    goto cleanup;
  }

  // Transmit SW code bytes, check received CRC, and ack if correct
  if (!prv_download_patch_from_system_resources(bootloader_size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to load second stage BLE bootloader");
    success = false;
    goto cleanup;
  }

cleanup:
  // We have loaded the image, disable the peripheral
  prv_deinit_dialog_bootrom_interface();
#if ACCESSORY_UART_IS_SHARED_WITH_BT
  // We are sharing a UART with the accessory port, so we need to block it from being used while we
  // are using it and then unblock at the end.
  accessory_unblock();
#endif

  if (success) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Succesfully loaded second stage BLE bootloader!");
  }
  return success;
}

bool dialog_bootrom_load_second_stage_bootloader(void) {
  // Note: It seems that the UART bootloader has a timeout between each
  // character receieved. If it does not get a response in time, it will
  // restart the bootrom sequence, searching for the device to talk to
  // resulting in USART OREs. While we could use a critical section this would
  // wind up blocking INTs for a little under a second which is too
  // long. During boot up, other things should not be running yet, but if we
  // need to reset the chip while everything is booted it's possible we don't
  // service a message before the timeout. Thus, retry a couple times to bring
  // the BLE chip out of reset before admitting defeat.
  int retries = 0;
  const int max_retries = 3;
  while (retries++ < max_retries) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Starting BLE Second Stage Bootloader, attempt %d", retries);
    if (prv_load_second_stage_bootloader()) {
      PBL_LOG(LOG_LEVEL_INFO, "BLE Second Stage Bootloader loaded in %d attempts!", retries);
      return true;
    }
  }

  return false;
}
