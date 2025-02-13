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

#include "kernel/pbl_malloc.h"
#include "comm/bluetooth_analytics.h"

#include <bluetooth/gap_le_connect.h>
#include <bluetooth/gatt.h>

// Dumb layer that passes payload directly to bt_driver. The payload are types from
// "gap_le_connect.h"
void hc_endpoint_gap_le_connect_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_GapLEConnect_ConnectionComplete: {
      const HcGapLeConnectionData *e = (HcGapLeConnectionData *)msg->payload;
      bt_driver_handle_le_connection_complete_event(&e->connection_complete_event);
      const GattDeviceConnectionEvent gatt_event = {
        .dev_address = e->connection_complete_event.peer_address.address,
        .connection_id = 0, /* FIXME: Bluetopia-only construct */
        .mtu = e->mtu
      };
      bt_driver_cb_gatt_handle_connect(&gatt_event);
      bluetooth_analytics_handle_connection_disconnection_event(
          AnalyticsEvent_BtLeConnectionComplete, e->connection_complete_event.status, NULL);
      break;
    }
    case HcMessageID_GapLEConnect_DisconnectionComplete: {
      const BleDisconnectionCompleteEvent *e = (BleDisconnectionCompleteEvent *)msg->payload;
      GattDeviceDisconnectionEvent gatt_event = {
        .dev_address = e->peer_address.address,
      };
      bt_driver_cb_gatt_handle_disconnect(&gatt_event);
      bt_driver_handle_le_disconnection_complete_event(e);
      break;
    }
    case HcMessageID_GapLEConnect_EncryptionChange: {
      const BleEncryptionChange *e = (BleEncryptionChange *)msg->payload;
      bt_driver_handle_le_encryption_change_event(e);
      break;
    }
    case HcMessageID_GapLEConnect_UpdateAddressAndIRK: {
      const BleAddressAndIRKChange *e = (BleAddressAndIRKChange *)msg->payload;
      bt_driver_handle_le_connection_handle_update_address_and_irk(e);
      break;
    }
    case HcMessageID_GapLEConnect_PeerVersionInfo: {
      const BleRemoteVersionInfoReceivedEvent *e =
          (BleRemoteVersionInfoReceivedEvent *)msg->payload;
      bt_driver_handle_peer_version_info_event(e);
      break;
    }
  }
}

int hc_endpoint_gap_le_disconnect(const BTDeviceInternal *address) {
  HcProtocolMessage *resp = hc_protocol_enqueue_with_payload_and_expect(
                              HcEndpointID_GapLEConnect, HcMessageID_GapLEConnect_Disconnect,
                              (const uint8_t *)address, sizeof(BTDeviceInternal));
  uint8_t rv = resp->payload[0];
  kernel_free(resp);
  return rv;
}
