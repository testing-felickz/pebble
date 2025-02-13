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

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gap_le_connect.h>
#include <bluetooth/hci_types.h>

#include "ble_common.h"
#include <stdbool.h>

addr_type_t dialog_utils_local_addr_type_to_dialog(const BTDeviceInternal *address);

bool dialog_utils_dialog_is_addr_type_random(addr_type_t addr_type);

void dialog_utils_bd_address_to_bt_device(const bd_address_t *addr, BTDeviceInternal *device_out);

void dialog_utils_bt_device_to_bd_address(const BTDeviceInternal *device, bd_address_t *addr_out);

HciStatusCode ble_error_to_hci_status_error(ble_error_t e);
