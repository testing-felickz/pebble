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
#include "hc_protocol/hc_protocol.h"

#include "connection.h"
#include "system/logging.h"

// Dialog SDK:
#include "ble_common.h"
#include "ble_gap.h"

#include <bluetooth/hci_types.h>

#include <stdint.h>

void pair_reply(uint16_t conn_idx, bool is_confirmed) {
  ble_error_t e = ble_gap_pair_reply(conn_idx,
                                     is_confirmed /* should_accept */,
                                     is_confirmed /* should_bond */);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_pair_reply: %d", e);
  }
}

static void prv_handle_pairing_response(const HcProtocolMessagePairingResponsePayload *response) {
  Connection *connection = connection_by_address(&response->device);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_WARNING, "Got pairing response, but disconnected in the mean time.");
    return;
  }

  uint16_t conn_idx = connection_get_idx(connection);
  pair_reply(conn_idx, response->is_confirmed);
}

void hc_endpoint_pairing_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Pairing_PairingResponse:
      prv_handle_pairing_response((const HcProtocolMessagePairingResponsePayload *)msg->payload);
      break;

    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unexpected cmd ID: %d", msg->command_id);
      break;
  }
}

void hc_endpoint_pairing_send_pairing_request(const BTDeviceInternal *device) {
  hc_protocol_enqueue_with_payload(HcEndpointID_Pairing, HcMessageID_Pairing_PairingRequest,
                                   (const uint8_t *)device, sizeof(*device));
}

void hc_endpoint_pairing_send_pairing_complete(const BTDeviceInternal *device,
                                               HciStatusCode status) {
  const HcProtocolMessagePairingCompletePayload payload = {
    .device = *device,
    .status = status,
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_Pairing, HcMessageID_Pairing_PairingComplete,
                                   (const uint8_t *)&payload, sizeof(payload));
}
