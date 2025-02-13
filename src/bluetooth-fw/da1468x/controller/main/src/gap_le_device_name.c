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

#include "connection.h"
#include "gatt_wrapper.h"
#include "system/logging.h"
#include "hc_protocol/hc_endpoint_gap_service.h"

#include "att.h"
#include "ble_att.h"
#include "ble_common.h"
#include "ble_gap.h"
#include "ble_gattc.h"
#include "ble_uuid.h"

#include <bluetooth/bluetooth_types.h>

#include <inttypes.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

void gap_le_device_name_handle_set(const char *name) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Setting Local Device Name: %s", name);
  ble_error_t e = ble_gap_device_name_set(name, ATT_PERM_READ);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Unexpected error setting name: %d", (int)e);
  }
}

static void prv_gap_le_device_name_request(Connection *connection) {
  att_uuid_t name_id;
  ble_uuid_create16(ATT_CHAR_DEVICE_NAME, &name_id);

  uint16_t conn_idx = connection_get_idx(connection);

  ble_error_t e = gatt_wrapper_read_by_uuid(conn_idx, ATT_1ST_REQ_START_HDL, ATT_1ST_REQ_END_HDL,
                                            &name_id,
                                            (uintptr_t)hc_endpoint_gap_service_device_name_read,
                                            GattReqSourceController);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_WARNING, "Unexpected error requesting remote device name (conn_idx %d): %d",
            conn_idx, e);
  }
}

void gap_le_device_name_handle_request(const BTDeviceInternal *addr) {
  Connection *connection = connection_by_address(addr);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_WARNING, "No connection for dev_name_request: " BT_DEVICE_ADDRESS_FMT,
            BT_DEVICE_ADDRESS_XPLODE_PTR(&addr->address));
    return;
  }
  prv_gap_le_device_name_request(connection);
}

static void prv_handle_request_all_cb(Connection *connection, void *data) {
  prv_gap_le_device_name_request(connection);
}

void gap_le_device_name_handle_request_all(void) {
  connection_for_each(prv_handle_request_all_cb, NULL);
}
