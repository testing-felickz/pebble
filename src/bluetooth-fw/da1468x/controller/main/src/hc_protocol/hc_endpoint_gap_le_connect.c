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

#include "hc_protocol/hc_endpoint_gap_le_connect.h"

#include "ble_common.h"
#include "ble_gap.h"

#include <bluetooth/gap_le_connect.h>
#include "connection.h"
#include "system/logging.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>


void hc_endpoint_gap_le_connect_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_GapLEConnect_Disconnect: {
      ble_error_t err = BLE_ERROR_NOT_CONNECTED;
      Connection *connection = connection_by_address((const BTDeviceInternal *)&msg->payload[0]);
      if (connection) {
        err = ble_gap_disconnect(connection_get_idx(connection),
                                 BLE_HCI_ERROR_REMOTE_USER_TERM_CON);
      }
      hc_protocol_enqueue_response(msg, (uint8_t *)&err, sizeof(ble_error_t));
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcGapLeConnect: unhandled message id: %d", msg->command_id);
    }
  }
}

static void prv_send_msg(HcMessageID_GapLEConnect command_id, void *payload, size_t payload_size) {
  hc_protocol_enqueue_with_payload(HcEndpointID_GapLEConnect, command_id, payload, payload_size);
}

void hc_endpoint_gap_le_connect_send_connection_complete(HcGapLeConnectionData *e) {
  prv_send_msg(HcMessageID_GapLEConnect_ConnectionComplete, e, sizeof(*e));
}

void hc_endpoint_gap_le_connect_send_disconnection_complete(BleDisconnectionCompleteEvent *e) {
  prv_send_msg(HcMessageID_GapLEConnect_DisconnectionComplete, e, sizeof(*e));
}

void hc_endpoint_gap_le_connect_send_encryption_changed(BleEncryptionChange *e) {
  prv_send_msg(HcMessageID_GapLEConnect_EncryptionChange, e, sizeof(*e));
}

void hc_endpoint_gap_le_connect_send_address_and_irk_changed(BleAddressAndIRKChange *e) {
  prv_send_msg(HcMessageID_GapLEConnect_UpdateAddressAndIRK, e, sizeof(*e));
}

void hc_endpoint_gap_le_connect_send_peer_version_info(BleRemoteVersionInfoReceivedEvent *e) {
  prv_send_msg(HcMessageID_GapLEConnect_PeerVersionInfo, e, sizeof(*e));
}
