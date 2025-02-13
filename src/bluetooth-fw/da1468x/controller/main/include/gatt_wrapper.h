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

#include "ble_gattc.h"

#include <bluetooth/bluetooth_types.h>

#include <stdint.h>

typedef enum GattReqSource {
  GattReqSourceHost,
  GattReqSourceController,
} GattReqSource;

// Gatt wrappers that call the underlying Dialog Gatt API's.
// Wrapped because it also keeps track of the context_ref in a `Connection` object
ble_error_t gatt_wrapper_read(uint16_t conn_idx, uint16_t handle, uintptr_t context_ref,
                              GattReqSource source);

ble_error_t gatt_wrapper_read_by_uuid(uint16_t conn_idx, uint16_t start_h, uint16_t end_h,
                                      const att_uuid_t *uuid, uintptr_t context_ref,
                                      GattReqSource source);

ble_error_t gatt_wrapper_write(uint16_t conn_idx, uint16_t handle, uint16_t length,
                               const uint8_t *value, uintptr_t context_ref, GattReqSource source);

ble_error_t gatt_wrapper_write_no_resp(uint16_t conn_idx, uint16_t handle, uint16_t length,
                                       const uint8_t *value);

// Gatt wrapper callback -- for use when calling gatt_wrapper_read|write from the controller.
// Pass the callback fn pointer in context_ref.
typedef void (*gatt_wrapper_read_cb)(const ble_evt_gattc_read_completed_t *evt);
typedef void (*gatt_wrapper_write_cb)(const ble_evt_gattc_write_completed_t *evt);

//
// Event Handlers (from ble_task)
//
void gatt_wrapper_handle_read_completed(const ble_evt_gattc_read_completed_t *evt);

void gatt_wrapper_handle_write_completed(const ble_evt_gattc_write_completed_t *evt);

void gatt_wrapper_handle_notification(const ble_evt_gattc_notification_t *evt);

void gatt_wrapper_handle_indication(const ble_evt_gattc_indication_t *evt);
