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

#include "service_changed.h"

// Dialog SDK:

#include "ble_gap.h"
#include "ble_gatt.h" // for GATT_CCC_INDICATIONS
#include "ble_gatts.h"
#include "ble_storage.h"
#include "storage.h" // for STORAGE_KEY_SVC_CHANGED_CCC

#include <util/size.h>

void service_changed_send_indication_to_all(uint16_t start_handle, uint16_t end_handle) {
  gap_device_t devices[BLE_GAP_MAX_CONNECTED];
  size_t length = ARRAY_LENGTH(devices);
  ble_gap_get_devices(GAP_DEVICE_FILTER_CONNECTED, NULL, &length, devices);

  for (int i = 0; i < (int)length; ++i) {
    const gap_device_t *const device = &devices[i];
    uint16_t svc_chg_ccc = 0;
    ble_storage_get_u16(device->conn_idx, STORAGE_KEY_SVC_CHANGED_CCC, &svc_chg_ccc);
    const bool is_subscribed_to_service_changed = !!(svc_chg_ccc & GATT_CCC_INDICATIONS);
    if (is_subscribed_to_service_changed) {
      ble_gatts_service_changed_ind(device->conn_idx, start_handle, end_handle);
    }
  }
}
