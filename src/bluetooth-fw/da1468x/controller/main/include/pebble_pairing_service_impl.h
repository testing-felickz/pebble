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

#include <stdint.h>

typedef struct Connection Connection;

void pebble_pairing_service_init(void);

//! Registers the Pebble Pairing Service with the ROM stack.
//! This needs to happen every time the ROM stack modifies the device configuration, see
//! notes in ble_gap.h.
void pebble_pairing_service_register(uint16_t start_hdl);

void pebble_pairing_service_handle_status_change(Connection *connection, uint16_t conn_idx);

void pebble_pairing_service_handle_gatt_mtu_change(Connection *connection, uint16_t conn_idx);

void pebble_pairing_service_handle_conn_params_change(Connection *connection, uint16_t conn_idx);
