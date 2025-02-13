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

#include "dialog_spi.h"

#include "board/board.h"
#include "drivers/dma.h"
#include "drivers/gpio.h"
#include "drivers/periph_config.h"
#include "drivers/spi.h"
#include "drivers/spi_dma.h"
#include "kernel/util/delay.h"
#include "kernel/util/stop.h"
#include "system/passert.h"
#include "util/units.h"

#define STM32F2_COMPATIBLE
#define STM32F4_COMPATIBLE
#define STM32F7_COMPATIBLE
#include <mcu.h>

#include "FreeRTOS.h"

#define DIALOG_INT_CONFIG_PTR (&BOARD_CONFIG_BT_COMMON.wakeup.int_gpio)
#define DIALOG_EXTI           (BOARD_CONFIG_BT_COMMON.wakeup.int_exti)

static DialogSpiDmaTransferCompletedCallback s_dma_transfer_complete_cb;
static bool s_dma_pending_transfer;

//
// INT line EXTI Handler
//
// FIXME: Pretty naive at the moment. Just busy loops waiting for INT
// to fire and could get stuck in an infinite loop if it never fires
//

// NB: this function is used only by the bootloader.
static volatile int s_int_fired;
void dialog_spi_wait_for_int(void) {
  while (s_int_fired == 0) {
    delay_us(1000);
  }
  __disable_irq();
  s_int_fired--;
  __enable_irq();
}

// NB: this function is used only by the bootloader.
static void prv_exti_cb(bool *should_context_switch) {
  s_int_fired++;

  if (s_int_fired > 1) {
    dbgserial_putstr("BLE Chip multiple INTS ?");
  }
}

static void prv_init_exti(ExtiHandlerCallback int_exti_cb) {
  gpio_input_init(DIALOG_INT_CONFIG_PTR);
  if (gpio_input_read(DIALOG_INT_CONFIG_PTR)) {
    int_exti_cb(NULL);
  }

  exti_configure_pin(DIALOG_EXTI, ExtiTrigger_Rising, int_exti_cb);
  exti_enable(DIALOG_EXTI);
}

void dialog_spi_set_pending_int(void) {
  exti_set_pending(DIALOG_EXTI);
}

void dialog_spi_indicate_data_ready_to_tx(void) {
  // Note: Double purposing the CS as an INT and SPI CS confused the SPI
  // driver. For now just treat it as a normal gpio when indicating data is
  // ready to be sent
  gpio_output_set(&BOARD_CONFIG_BT_SPI.cs, true);
}

void dialog_spi_assert_chip_select(bool asserted) {
  if (asserted) {
    spi_ll_slave_scs_assert(DIALOG_SPI);
  } else {
    spi_ll_slave_scs_deassert(DIALOG_SPI);
  }
}

//
// Low Level SPI TX/RX config
//

static void prv_enable_spi_clock(void) {
  spi_ll_slave_acquire(DIALOG_SPI);
  spi_ll_slave_drive_clock(DIALOG_SPI, false);
}

static void prv_disable_spi_clock(void) {
  // When the SPI peripheral is disabled, the CLK line floats. Let's
  // reconfigure the CLK GPIO to be active low so the Dialog BT controller
  // doesn't see a false clock edge
  spi_ll_slave_drive_clock(DIALOG_SPI, true);
  spi_ll_slave_release(DIALOG_SPI);
}

static void prv_init_spi(void) {
  spi_slave_port_init(DIALOG_SPI);
}

//
// DMA
//

static void prv_cleanup_after_dma_transfer(void) {
  dialog_spi_assert_chip_select(false);

  // FIXME: PBL-32865: The "OVR" (overrun) status flag gets set at some point, not sure why because
  // the data arrives OK. Need to reset it, otherwise the next transfer will get a 0x00 byte
  // prepended to the beginning...
  // From the F7 Spec: "When the SPI is used only to transmit data, it is possible to enable only
  // the SPI Tx DMA channel. In this case, the OVR flag is set because the data received is not
  // read." I assume this will only happen if more than 4 (FIFO size) bytes are read. NB: They will
  // likely remain in the RX FIFO, ready to mess up your next transfer.

  spi_ll_slave_clear_errors(DIALOG_SPI);

  spi_ll_slave_spi_disable(DIALOG_SPI);
  prv_disable_spi_clock();
  stop_mode_enable(InhibitorBluetooth);
}

static bool prv_dma_handler(const SPISlavePort *slave, void *context) {
  bool should_context_switch = false;
  if (!s_dma_pending_transfer) {
    return should_context_switch;
  }
  s_dma_pending_transfer = false;

  // If the SPI frequency is set low (for debugging) for example 800KHz, it is possible that the
  // DMA transfer complete interrupts have fired, but the SPI peripheral is still busy clocking the
  // data in/out. With 3.3MHz SPI clock frequency, this is not going to loop at all.
  spi_slave_wait_until_idle_blocking(DIALOG_SPI);

  prv_cleanup_after_dma_transfer();

  if (s_dma_transfer_complete_cb) {
    s_dma_transfer_complete_cb(&should_context_switch);
  }
  return should_context_switch;
}

void dialog_spi_send_and_receive_dma(const void *tx_buffer, void *rx_buffer, size_t size,
                                     DialogSpiDmaTransferCompletedCallback done_isr_callback) {
  PBL_ASSERTN(tx_buffer || rx_buffer);

  // FIXME: PBL-32864: Ugh, can't do this from ISR
//  periph_config_acquire_lock();

  s_dma_transfer_complete_cb = done_isr_callback;

  prv_enable_spi_clock();
  // Client code is supposed to ensure no transfer gets set up,
  // while the previous one is still on-going:
  PBL_ASSERTN(!spi_ll_slave_dma_in_progress(DIALOG_SPI));
  spi_ll_slave_spi_disable(DIALOG_SPI);

  stop_mode_disable(InhibitorBluetooth);

  // We're about to run the SPI clock, enable CS:
  dialog_spi_assert_chip_select(true);

  s_dma_pending_transfer = true;

  spi_ll_slave_spi_enable(DIALOG_SPI);
  spi_ll_slave_read_write_dma_start(DIALOG_SPI, tx_buffer, rx_buffer, size, prv_dma_handler, NULL);

  // Don't do any set up after this point, chances are the DMA transfer complete interrupt fires
  // before any code that you've put here finishes...

  // FIXME: PBL-32864: Ugh, can't do this from ISR
//  periph_config_release_lock();
}

void dialog_spi_start_cmd(void) {
  prv_enable_spi_clock();
  dialog_spi_assert_chip_select(true);
}

void dialog_spi_end_cmd(void) {
  dialog_spi_assert_chip_select(false);
  prv_disable_spi_clock();
}

uint8_t dialog_spi_send_and_receive_byte(uint8_t byte) {
  return spi_ll_slave_read_write(DIALOG_SPI, byte);
}

void dialog_spi_transmit_data_no_wait(uint8_t *send, size_t buf_len) {
  dialog_spi_start_cmd();

  for (uint32_t i = 0; i < buf_len; i++) {
    dialog_spi_send_and_receive_byte(send[i]);
  }

  dialog_spi_end_cmd();
}

// NB: this function is used only by the bootloader.
void dialog_spi_transmit_data(uint8_t *send, size_t buf_len) {
  // Don't tx until dialog says its ready!
  dialog_spi_wait_for_int();
  dialog_spi_transmit_data_no_wait(send, buf_len);
}

void dialog_spi_receive_data_no_wait(uint8_t *receive_buf, size_t buf_len) {
  dialog_spi_start_cmd();

  for (size_t i = 0; i < buf_len; i++) {
    receive_buf[i] = dialog_spi_send_and_receive_byte(0);
  }

  dialog_spi_end_cmd();
}

// NB: this function is used only by the bootloader.
void dialog_spi_receive_data(uint8_t *receive_buf, size_t buf_len) {
  // Dialog will always signal when it has data ready to send
  dialog_spi_wait_for_int();
  dialog_spi_receive_data_no_wait(receive_buf, buf_len);
}

void dialog_spi_init(ExtiHandlerCallback int_exti_cb) {
  // reset the interrupt count
  s_int_fired = 0;
  s_dma_pending_transfer = false;

  periph_config_acquire_lock();
  prv_init_spi();
  periph_config_release_lock();

  prv_init_exti(int_exti_cb ?: prv_exti_cb);
}

void dialog_spi_deinit(void) {
  exti_disable(DIALOG_EXTI);

  if (s_dma_pending_transfer) {
    spi_ll_slave_read_write_dma_stop(DIALOG_SPI);
    prv_cleanup_after_dma_transfer();
    s_dma_pending_transfer = false;
  }

  spi_slave_port_deinit(DIALOG_SPI);
}
