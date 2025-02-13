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

#include "hc_protocol/hc_endpoint_pairing.h"

#include "comm/ble/gap_le_connection.h"
#include "comm/bt_conn_mgr.h"
#include "comm/bt_lock.h"
#include "kernel/pbl_malloc.h"
#include "comm/bluetooth_analytics.h"
#include "system/logging.h"

#include <bluetooth/hci_types.h>
#include <bluetooth/pairing_confirm.h>

void bt_driver_pairing_confirm(const PairingUserConfirmationCtx *ctx,
                               bool is_confirmed) {
  GAPLEConnection *connection = (GAPLEConnection *)ctx;
  if (!gap_le_connection_is_valid(connection)) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection found for pairing confirm");
    return;
  }
  hc_endpoint_pairing_send_pairing_response(&connection->device, is_confirmed);
}

void pairing_confirm_handle_request(const BTDeviceInternal *device) {
  GAPLEConnection *connection = gap_le_connection_by_device(device);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection found for pairing request");
    return;
  }

  // PBL-38595: Make the connection fast during the pairing:
  const uint16_t max_period_secs = 40;
  conn_mgr_set_ble_conn_response_time(connection, BtConsumerLePairing,
                                      ResponseTimeMin, max_period_secs);

  PairingUserConfirmationCtx *ctx = (PairingUserConfirmationCtx *)connection;
  bt_driver_cb_pairing_confirm_handle_request(ctx, NULL /* device name */,
                                              NULL /* "just works", so no token */);

  bluetooth_analytics_handle_ble_pairing_request();
}

void pairing_confirm_handle_complete(const BTDeviceInternal *device, HciStatusCode status) {
  const PairingUserConfirmationCtx *ctx;
  bt_lock();
  {
    ctx = (const PairingUserConfirmationCtx *)gap_le_connection_by_device(device);
  }
  bt_unlock();
  if (!ctx) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection found for pairing complete");
    return;
  }

  const bool success = (status == HciStatusCode_Success);
  if (!success) {
    bluetooth_analytics_handle_ble_pairing_error(status);
    PBL_LOG(LOG_LEVEL_ERROR, "Pairing failed w/ status: 0x%x", status);
  }

  bt_driver_cb_pairing_confirm_handle_completed(ctx, success);
}
