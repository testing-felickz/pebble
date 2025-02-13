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

#include "dialog_utils.h"

// Dialog SDK:
#include "ble_common.h"

#include <bluetooth/gap_le_connect.h>
#include <string.h>
#include <stdbool.h>

bool dialog_utils_dialog_is_addr_type_random(addr_type_t addr_type) {
  return (addr_type == PRIVATE_ADDRESS);
}

addr_type_t dialog_utils_local_addr_type_to_dialog(const BTDeviceInternal *address) {
  return (address->is_random_address) ? PRIVATE_ADDRESS : PUBLIC_ADDRESS;
}

void dialog_utils_bd_address_to_bt_device(const bd_address_t *addr, BTDeviceInternal *device_out) {
  *device_out = (BTDeviceInternal) {};
  device_out->is_random_address = dialog_utils_dialog_is_addr_type_random(addr->addr_type);
  memcpy(&device_out->address, &addr->addr, sizeof(device_out->address));
}

void dialog_utils_bt_device_to_bd_address(const BTDeviceInternal *device, bd_address_t *addr_out) {
  addr_out->addr_type = dialog_utils_local_addr_type_to_dialog(device);
  memcpy(addr_out->addr, &device->address, sizeof(addr_out->addr));
}

HciStatusCode ble_error_to_hci_status_error(ble_error_t e) {
  switch (e) {
    case BLE_STATUS_OK:
      return HciStatusCode_Success;
    default:
      return (e + HciStatusCode_VS_Base);
  }
}
