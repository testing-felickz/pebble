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

#include "gatt_wrapper.h"
#include "gap_le_device_name_impl.h"
#include "hc_protocol/hc_endpoint_gap_service.h"
#include "local_addr_impl.h"
#include "pra_generate.h"

#include "system/logging.h"
#include "system/hexdump.h"

#include <util/attributes.h>

// Dialog SDK:
#include "att.h"
#include "ble_gap.h"
#include "ble_gattc.h"
#include "ble_uuid.h"

#include <string.h>

void hc_endpoint_gap_service_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_GapService_SetName:
      gap_le_device_name_handle_set((const char *)&msg->payload[0]);
      break;
    case HcMessageID_GapService_DeviceNameRequest: {
        BTDeviceInternal addr;
        memcpy(&addr, &msg->payload[0], sizeof(BTDeviceInternal));
        gap_le_device_name_handle_request(&addr);
      }
      break;
    case HcMessageID_GapService_DeviceNameRequest_All:
      gap_le_device_name_handle_request_all();
      break;
    case HcMessageID_GapService_SetLocalAddress: {
      const HcProtocol_GapServiceSetLocalAddress *payload =
          (const HcProtocol_GapServiceSetLocalAddress *)msg->payload;
      local_addr_set(payload->allow_cycling,
                     payload->allow_cycling ? NULL : &payload->pinned_addr);
      break;
    }
    case HcMessageID_GapService_GeneratePrivateResolvable_address: {
      HcProtocol_GapServiceGeneratePrivateResolvableAddressResponse payload;
      pra_generate(&payload.address);
      hc_protocol_enqueue_response(msg, (const uint8_t *)&payload, sizeof(payload));
      break;
    }
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcGap: unhandled message id: %d", msg->command_id);
  }
}

void hc_endpoint_gap_service_mtu_changed(const Connection *connection, uint16_t mtu) {
  HcProtocol_GapServiceMtuChanged mtu_resp = {};
  connection_get_address(connection, &mtu_resp.addr);
  mtu_resp.mtu = mtu;
  hc_protocol_enqueue_with_payload(HcEndpointID_GapService, HcMessageId_GapService_MtuChanged,
                                   (const uint8_t  *)&mtu_resp, sizeof(mtu_resp));
}

void hc_endpoint_gap_service_device_name_read(const ble_evt_gattc_read_completed_t *evt) {
  if (evt->status != ATT_ERROR_OK) {
    PBL_LOG(LOG_LEVEL_WARNING, "Read_device_name failed: Idx: %d Att 0x%x Status %d",
            evt->conn_idx, evt->handle, evt->status);
    return;
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Read_device_name: Idx: %d Handle 0x%x Offset %d Length %d Data:",
          evt->conn_idx, evt->handle, evt->offset, evt->length);
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, evt->value, evt->length);
  Connection *connection = connection_by_idx(evt->conn_idx);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_WARNING, "Read_device_name: Failed to find connection");
    return;
  }

  typedef struct PACKED HcProtocol_GapDeviceNameResponse {
    HcProtocol_GapDeviceNameResponseHeader header;
    uint8_t name[evt->length];
  } HcProtocol_GapDeviceNameResponse;
  HcProtocol_GapDeviceNameResponse response;

  connection_get_address(connection, &response.header.addr);
  response.header.name_length = evt->length;
  memcpy(response.name, evt->value, evt->length);

  hc_protocol_enqueue_with_payload(HcEndpointID_GapService,
                                   HcMessageID_GapService_DeviceNameRequest,
                                   (void *)&response, sizeof(HcProtocol_GapDeviceNameResponse));
}
