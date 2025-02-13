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

#include "hc_protocol/hc_endpoint_discovery.h"

#include "connection.h"
#include "hc_protocol/hc_protocol.h"
#include "system/logging.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/hci_types.h>

// Dialog APIs
#include "ble_common.h"
#include "ble_gattc.h"

void hc_endpoint_discovery_complete(const BTDeviceInternal *address, HciStatusCode status) {
  HcProtocolDiscoveryCompletePayload payload = {
    .address = *address,
    .status = status
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_Discovery, HcMessageID_Discovery_Complete,
                                   (uint8_t *)&payload, sizeof(payload));
}

static void prv_handle_discovery_start(const HcProtocolDiscoveryStartPayload *req) {
  PBL_LOG(LOG_LEVEL_DEBUG, "->" BT_DEVICE_ADDRESS_FMT,
          BT_DEVICE_ADDRESS_XPLODE(req->address.address));
  Connection *conn = connection_by_address(&req->address);
  HciStatusCode status = HciStatusCode_Success;
  if (conn == NULL) {
    status = HciStatusCode_UnknownConnectionIdentifier;
    PBL_LOG(LOG_LEVEL_WARNING, "No connection to addr!");
    goto failure;
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Received Discovery Start for 0x%x to 0x%x",
          (int)req->range.start, (int)req->range.end);

  uint16_t conn_idx = connection_get_idx(conn);
  ble_error_t e = ble_gattc_browse(conn_idx, NULL, req->range.start, req->range.end);
  if (e != BLE_STATUS_OK) {
    status = HciStatusCode_VS_Base + e;
    PBL_LOG(LOG_LEVEL_DEBUG, "ble_gattc_browse: %u", e);
    goto failure;
  }
  return;
failure:
  // Notify Host that discovery terminated unexpectedly
  hc_endpoint_discovery_complete(&req->address, status);
}

void hc_endpoint_discovery_service_changed_handle(const BTDeviceInternal *address,
                                                  uint16_t handle) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Gatt Service Discovery Service Changed Handle: %d", handle);

  HcProtocolDiscoveryServiceChangedHandlePayload payload = {
    .address = *address,
    .handle = handle
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_Discovery,
                                   HcMessageID_Discovery_Service_Changed_Handle,
                                   (uint8_t *)&payload, sizeof(payload));
}

static void prv_handle_discovery_stop(const BTDeviceInternal *address) {
  Connection *conn = connection_by_address(address);

  if (conn == NULL) {
    return;
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Received Discovery Stop Request");

  uint16_t conn_idx = connection_get_idx(conn);
  ble_error_t e = ble_gattc_discover_cancel(conn_idx);

  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_DEBUG, "ble_gattc_discover_cancel: %u", e);
  }
}

void hc_endpoint_discovery_send_service_found(
    const HcProtocolDiscoveryServiceFoundPayload *payload, uint32_t payload_size) {
  // Note: kind of wasteful double copy here, a service node could get sort of
  // big (several hundred bytes)
  hc_protocol_enqueue_with_payload(HcEndpointID_Discovery, HcMessageID_Discovery_Service_Found,
                                   (uint8_t *)payload, payload_size);
}

void hc_endpoint_discovery_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Discovery_Start:
      prv_handle_discovery_start((HcProtocolDiscoveryStartPayload *)msg->payload);
      break;
    case HcMessageID_Discovery_Stop:
      prv_handle_discovery_stop((BTDeviceInternal *)msg->payload);
      break;
    default:
      PBL_LOG(LOG_LEVEL_WARNING, "Unexpected command: 0x%x", (int)msg->command_id);
  }
}
