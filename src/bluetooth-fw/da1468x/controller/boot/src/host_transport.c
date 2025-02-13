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

#include <string.h>
#include <stdint.h>

#include "da1468x_mem_map.h"

#include "debug_print.h"
#include "hw_gpio.h"
#include "hw_spi.h"

#include "board.h"
#include "host_transport.h"
#include "util/crc32.h"

// Dialog SPI bootloader implementation
//
// See following doc for protocol specification:
//  https://docs.google.com/document/d/1PrnTsDhBZYsrlxa9-6OzdoEvtE50uVkSTufqEdQ2yWw/

// The arm vector table has a default size of 0x40 bytes. The rest of the space
// is variable depending on the number of IRQn interrupt handlers implemented
// by the platform. For the dialog part 32 IRQs are provided. There is also a
// patch area used by the dialog BT ROM which must be loaded and comes directly
// after the vector table, it's 128 bytes
#define IVT_TABLE_SIZE (0x40 + 32 * 4 + 0x80)
static uint8_t s_vector_table[IVT_TABLE_SIZE];

static volatile bool s_expected_bytes_received = false;
static void prv_expect_byte_spi_int_cb(void *user_data, uint16_t transferred) {
  s_expected_bytes_received = true;
  debug_print_str_and_int("Bytes TX/RXed: ", transferred);
}

// Interesting observation: The INT fires on writes once the data has been drained to the FIFO,
// not when it actually gets drained.
static void prv_write_or_read_bytes(void *byte_buffer, int num_bytes, bool do_write) {
    s_expected_bytes_received = false;
    if (do_write) {
      hw_spi_write_buf(HOST_SPI->spi.peripheral,
                       byte_buffer, num_bytes, prv_expect_byte_spi_int_cb, NULL);
    } else {
      hw_spi_read_buf(HOST_SPI->spi.peripheral,
                      byte_buffer, num_bytes, prv_expect_byte_spi_int_cb, NULL);
    }

    hw_gpio_set_active(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin);
    // Probably not necessary but add a little delay so its easier to catch INT
    // on logic analyzer
    for (volatile int delay = 0; delay < 50; delay++) { }

    while (!s_expected_bytes_received) { };
    hw_gpio_set_inactive(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin);
}

static void prv_handle_hi_command(void) {
    debug_print_str("HI CMD\n");
    uint8_t response[] = { 'H', 'E', 'R', 'E' };
    prv_write_or_read_bytes(response, sizeof(response), true);
}

static void prv_send_crc_of_mem_region(void *data_start, size_t len) {
    uint32_t crc = CRC32_INIT;
    crc = crc32(crc, data_start, len);

    prv_write_or_read_bytes((uint8_t *)&crc, sizeof(crc), true);
    debug_print_str_and_int("Computed CRC: ", (int)crc);
}

static void prv_handle_load_data_command(void) {
    debug_print_str("LD CMD\n");

    struct __attribute__((packed)) {
        uint32_t copy_address;
        uint32_t length;
    } send_data_cmd_payload = { };

    prv_write_or_read_bytes(&send_data_cmd_payload, sizeof(send_data_cmd_payload), false);

    debug_print_str_and_int(" Address:", send_data_cmd_payload.copy_address);
    debug_print_str_and_int(" Length:", send_data_cmd_payload.length);

    prv_write_or_read_bytes((uint8_t*)send_data_cmd_payload.copy_address,
                            send_data_cmd_payload.length, false);

    prv_send_crc_of_mem_region(
        (void *)send_data_cmd_payload.copy_address, send_data_cmd_payload.length);
}

static void prv_handle_vector_table_update_command(void) {
    debug_print_str("VT CMD\n");

    // reset vector table
    memset(s_vector_table, 0x0, sizeof(s_vector_table));

    uint8_t number_of_entries = 0;
    prv_write_or_read_bytes(&number_of_entries, sizeof(number_of_entries), false);

    size_t vector_table_copy_size = number_of_entries * 4;
    prv_write_or_read_bytes(s_vector_table, vector_table_copy_size, false);

    prv_send_crc_of_mem_region(&s_vector_table[0], vector_table_copy_size);
}

static void prv_handle_reboot_command(void) {
    debug_print_str("RT CMD\n");
    const uint32_t vt_start_addr = DATA_RAM_BASE_ADDRESS;

    // We are about to overwrite the vector table. Disable interrupts while
    // this is taking place
    __disable_irq();

    // Interrupt Clear Enable Register:
    NVIC->ICER[0] = ~0;
    // Interrupt Clear Pending Register:
    NVIC->ICPR[0] = ~0;

    for (size_t i = 0; i < sizeof(s_vector_table); i++) {
      *(uint8_t *)(vt_start_addr + i) = s_vector_table[i];
    }

    NVIC_SystemReset();
    __builtin_unreachable();
}

static void prv_bootloader_loop(void) {
  debug_print_str("Beginning Bootloader Loop\n");
  while (1) {
    uint8_t cmd[2] = { };
    prv_write_or_read_bytes(cmd, sizeof(cmd), false);

    if (cmd[0] == 'H' && cmd[1] == 'I') {
      prv_handle_hi_command();
    } else if (cmd[0] == 'L' && cmd[1] == 'D') {
      prv_handle_load_data_command();
    } else if (cmd[0] == 'V' && cmd[1] == 'T') {
      prv_handle_vector_table_update_command();
    } else if (cmd[0] == 'R' && cmd[1] == 'T') {
      prv_handle_reboot_command();
    } else {
      debug_print_str("Unknown CMD:");
      debug_print_str_and_int(" Byte 0:", cmd[0]);
      debug_print_str_and_int(" Byte 1:", cmd[1]);
    }
  }
}

static void prv_configure_pins_for_spi_transfer(void) {
  hw_gpio_set_pin_function(HOST_SPI->spi.cs.port, HOST_SPI->spi.cs.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.cs.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.cs_2.port, HOST_SPI->spi.cs_2.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.cs_2.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.clk.port, HOST_SPI->spi.clk.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.clk.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.mosi_di.port, HOST_SPI->spi.mosi_di.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.mosi_di.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.miso_do.port, HOST_SPI->spi.miso_do.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.miso_do.function);

  hw_gpio_configure_pin(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin,
                        HW_GPIO_MODE_OUTPUT, HOST_SPI->mcu_int.function, false);
}

static void prv_configure_spi_peripheral(void) {
  spi_config config = {
    .cs_pad = { 0, 0 },
    .word_mode = HW_SPI_WORD_8BIT,
    .smn_role = HW_SPI_MODE_SLAVE,
    .phase_mode = HW_SPI_PHA_MODE_0,
    .polarity_mode = HW_SPI_POL_LOW,
    .mint_mode = HW_SPI_MINT_DISABLE, // we are not using this feature
    .xtal_freq = 0,
    .fifo_mode = HW_SPI_FIFO_RX_TX,
    .disabled = 0,
#ifdef HW_SPI_DMA_SUPPORT
    .use_dma = 1,
    .rx_dma_channel = HOST_SPI_RX_DMA_CHANNEL,
    .tx_dma_channel = HOST_SPI_TX_DMA_CHANNEL,
#endif
  };

  hw_spi_init(HOST_SPI->spi.peripheral, &config);
}

void host_transport_begin(void) {
  prv_configure_pins_for_spi_transfer();

  prv_configure_spi_peripheral();

  prv_bootloader_loop();

  __builtin_unreachable();
}
