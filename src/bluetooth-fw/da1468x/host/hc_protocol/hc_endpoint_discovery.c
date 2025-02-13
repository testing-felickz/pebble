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

#include "comm/ble/gap_le_connection.h"
#include "comm/bt_lock.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include <bluetooth/gatt.h>
#include <bluetooth/gatt_discovery.h>
#include <bluetooth/bluetooth_types.h>

#include <stdbool.h>
#include <string.h>

bool hc_endpoint_discovery_start(const ATTHandleRange *range, const BTDeviceInternal *address) {
  HcProtocolDiscoveryStartPayload payload = {
    .address = *address,
    .range = *range,
  };

  return hc_protocol_enqueue_with_payload(
      HcEndpointID_Discovery, HcMessageID_Discovery_Start, (uint8_t *)&payload, sizeof(payload));
}

bool hc_endpoint_discovery_stop(const BTDeviceInternal *address) {
  return hc_protocol_enqueue_with_payload(
      HcEndpointID_Discovery, HcMessageID_Discovery_Stop, (uint8_t *)address, sizeof(*address));
}

static void prv_handle_new_service_found(const HcProtocolDiscoveryServiceFoundPayload *payload) {
  GAPLEConnection *connection = gap_le_connection_by_device(&payload->address);
  if (connection == NULL) {
    PBL_LOG(LOG_LEVEL_WARNING, "Service found but no device connected?");
    return;
  }

  uint32_t gatt_service_size = payload->service.size_bytes;
  GATTService *service_copy = kernel_malloc(gatt_service_size);
  BTErrno e = BTErrnoNotEnoughResources;
  if (service_copy) {
    e = BTErrnoOK;
    memcpy(service_copy, &payload->service, gatt_service_size);
  }

  bt_driver_cb_gatt_client_discovery_handle_indication(connection, service_copy, e);
}

static void prv_handle_discovery_complete(const HcProtocolDiscoveryCompletePayload *payload) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Discovery Complete, status = 0x%x", payload->status);
  GAPLEConnection *connection = gap_le_connection_by_device(&payload->address);
  if (connection == NULL) {
    PBL_LOG(LOG_LEVEL_WARNING, "Discovery complete but no device connected?");
    return;
  }

  BTErrno e = (payload->status == HciStatusCode_Success) ? BTErrnoOK :
      (BTErrnoInternalErrorBegin + payload->status);

  bt_driver_cb_gatt_client_discovery_complete(connection, e);
}

static void prv_handle_discover_service_changed_handle(
    HcProtocolDiscoveryServiceChangedHandlePayload *payload) {
  GAPLEConnection *connection = gap_le_connection_by_device(&payload->address);
  if (connection == NULL) {
    PBL_LOG(LOG_LEVEL_WARNING, "Discovery service_changed_handle but no device connected?");
    return;
  }

  bt_driver_cb_gatt_client_discovery_handle_service_changed(connection, payload->handle);
}

void hc_endpoint_discovery_handler(const HcProtocolMessage *msg) {
  bt_lock();
  {
    switch (msg->command_id) {
      case HcMessageID_Discovery_Service_Found:
        prv_handle_new_service_found((HcProtocolDiscoveryServiceFoundPayload *)msg->payload);
        break;
      case HcMessageID_Discovery_Complete:
        prv_handle_discovery_complete((HcProtocolDiscoveryCompletePayload *)msg->payload);
        break;
      case HcMessageID_Discovery_Service_Changed_Handle:
        prv_handle_discover_service_changed_handle(
          (HcProtocolDiscoveryServiceChangedHandlePayload *)msg->payload);
        break;
      default:
        PBL_LOG(LOG_LEVEL_WARNING, "Unexpected command: 0x%x", (int)msg->command_id);
    }
  }
  bt_unlock();
}
