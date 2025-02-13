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

#include "ble_gap.h"

#include "connection.h"

#include "dialog_utils.h"
#include "hc_protocol/hc_protocol.h"
#include "hc_protocol/hc_endpoint_responsiveness.h"
#include "system/logging.h"

#include <bluetooth/hci_types.h>

#include <inttypes.h>

static void prv_handle_update_request(const HcProtocolMessageResponsivenessPayload *req) {
  const BleConnectionParamsUpdateReq *params = &req->params;
  gap_conn_params_t conn_params = {
    .interval_min = params->interval_min_1_25ms,
    .interval_max = params->interval_max_1_25ms,
    .slave_latency = params->slave_latency_events,
    .sup_timeout = params->supervision_timeout_10ms,
  };

  Connection *conn = connection_by_address(&req->address);
  ble_error_t e = BLE_ERROR_NOT_CONNECTED;
  if (conn != NULL) {
    int conn_idx = connection_get_idx(conn);
    PBL_LOG(LOG_LEVEL_DEBUG, "Requesting conn param change, Conn Idx: %d - (%d %d %d %d)",
            conn_idx, (int)conn_params.interval_min, (int)conn_params.interval_max,
            (int)conn_params.slave_latency, (int)conn_params.sup_timeout);
    e = ble_gap_conn_param_update(conn_idx, &conn_params);
  }

  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error prv_handle_update_request: %d", (int)e);
    BleConnectionParams params = {};
    hc_endpoint_responsiveness_notify_update(
        &params, &req->address, ble_error_to_hci_status_error(e));
  }
}

void hc_endpoint_responsiveness_notify_update(
    const BleConnectionParams *params, const BTDeviceInternal *addr,
    HciStatusCode status) {
  uint8_t response_len = sizeof(HcProtocolMessage) +
      sizeof(BleConnectionUpdateCompleteEvent);

  uint8_t buf[response_len];
  HcProtocolMessage *msg = (HcProtocolMessage *)&buf[0];

  *msg = (HcProtocolMessage) {
    .message_length = response_len,
    .endpoint_id = HcEndpointID_Responsiveness,
    .command_id = HcMessageID_Id_ConnParamUpdateResponse,
  };

  BleConnectionUpdateCompleteEvent *payload =
      (BleConnectionUpdateCompleteEvent *)msg->payload;
  *payload = (BleConnectionUpdateCompleteEvent) {
    .status = status,
    .dev_address = addr->address,
  };

  if (params) {
    payload->conn_params = *params;
  }

  hc_protocol_enqueue(msg);
}

void hc_endpoint_responsiveness_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Id_ConnParamUpdateReq:
      prv_handle_update_request((HcProtocolMessageResponsivenessPayload *)msg->payload);
      break;
    default:
      PBL_LOG(LOG_LEVEL_WARNING, "Unexpected command: 0x%x", (int)msg->command_id);
  }
}
