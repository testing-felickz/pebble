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

#pragma once

#include "drivers/exti.h"

#include <stdbool.h>
#include <stdint.h>

void dialog_spi_init(ExtiHandlerCallback int_exti_cb);

void dialog_spi_transmit_data(uint8_t *send, size_t buf_len);
void dialog_spi_transmit_data_no_wait(uint8_t *send, size_t buf_len);

void dialog_spi_receive_data(uint8_t *receive_buf, size_t buf_len);
void dialog_spi_receive_data_no_wait(uint8_t *receive_buf, size_t buf_len);

void dialog_spi_wait_for_int(void);

void dialog_spi_set_pending_int(void);

void dialog_spi_assert_chip_select(bool asserted);

void dialog_spi_indicate_data_ready_to_tx(void);

void dialog_spi_start_cmd(void);

uint8_t dialog_spi_send_and_receive_byte(uint8_t byte);

typedef void (*DialogSpiDmaTransferCompletedCallback)(bool *should_context_switch);

//! @note Safe to call from ISR!
void dialog_spi_send_and_receive_dma(const void *tx_buffer, void *rx_buffer, size_t size,
                                     DialogSpiDmaTransferCompletedCallback done_isr_callback);

void dialog_spi_cancel_dma(void);

void dialog_spi_end_cmd(void);

void dialog_spi_deinit(void);
