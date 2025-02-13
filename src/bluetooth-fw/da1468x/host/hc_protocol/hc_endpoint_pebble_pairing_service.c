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

#include "hc_protocol/hc_endpoint_pebble_pairing_service.h"

#include "comm/bt_lock.h"
#include "system/logging.h"
#include "comm/ble/gap_le_connection.h"

#include <bluetooth/pebble_pairing_service.h>

void hc_endpoint_pebble_pairing_service_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_PebblePairingServiceiOSAppTerminationDetected:
      bt_driver_cb_pebble_pairing_service_handle_ios_app_termination_detected();
      break;

    case HcMessageID_PebblePairingServiceFoundGateway: {
      bt_lock();
      {
        BTDeviceInternal *device = (BTDeviceInternal *)&msg->payload[0];
        GAPLEConnection *connection = gap_le_connection_by_device(device);
        if (connection) {
          gap_le_connection_set_gateway(connection, true);
        }
      }
      bt_unlock();
      break;
    }

    case HcMessageID_PebblePairingServiceConnParams: {
      const HcPpsConnParamsPayload *payload = (const HcPpsConnParamsPayload *)&msg->payload[0];
      size_t params_length = msg->message_length - offsetof(HcPpsConnParamsPayload, conn_params);
      bt_driver_cb_pebble_pairing_service_handle_connection_parameter_write(
          &payload->device, &payload->conn_params, params_length);
      break;
    }

    default:
      PBL_LOG(LOG_LEVEL_ERROR,
              "Unknown hc_endpoint_pebble_pairing_service_handler cmdid %d", msg->command_id);
      break;
  }
}
