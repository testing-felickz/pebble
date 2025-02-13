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

#include "hc_protocol/hc_endpoint_responsiveness.h"
#include "bluetooth/responsiveness.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include "ble_common.h"

bool hc_endpoint_responsiveness_request_update(
    const BTDeviceInternal *addr, const BleConnectionParamsUpdateReq *params) {
  uint8_t message_len = sizeof(HcProtocolMessageResponsivenessPayload) + sizeof(HcProtocolMessage);
  uint8_t buf[message_len];

  HcProtocolMessage *msg = (HcProtocolMessage *)&buf[0];

  *msg = (HcProtocolMessage) {
    .message_length = message_len,
    .endpoint_id = HcEndpointID_Responsiveness,
    .command_id = HcMessageID_Id_ConnParamUpdateReq,
  };

  HcProtocolMessageResponsivenessPayload *payload =
      (HcProtocolMessageResponsivenessPayload *)msg->payload;
  *payload = (HcProtocolMessageResponsivenessPayload) {
    .address = *addr,
    .params = *params,
  };

  return hc_protocol_enqueue(msg);
}

void hc_endpoint_responsiveness_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Id_ConnParamUpdateResponse:
      bt_driver_handle_le_conn_params_update_event(
          (BleConnectionUpdateCompleteEvent *)&msg->payload);
      break;
    default:
      PBL_LOG(LOG_LEVEL_WARNING, "Unexpected command: 0x%x", (int)msg->command_id);
  }
}
