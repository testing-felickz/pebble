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

void host_transport_init_periph(void);
void host_transport_init(void);

typedef enum {
  SCSPinFunction_Wakeup_GPIO,
  SCSPinFunction_SPI_CS,
} SCSPinFunction;

// Used by core dump.
void host_transport_configure_spi_scs_pin(SCSPinFunction function);
void host_transport_set_mcu_int(bool is_ready_to_transact);
