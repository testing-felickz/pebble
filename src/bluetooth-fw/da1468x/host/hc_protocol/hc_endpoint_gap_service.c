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

#include "hc_protocol/hc_endpoint_gap_service.h"

#include "comm/ble/gap_le_connection.h"
#include "kernel/pbl_malloc.h"
#include "services/common/system_task.h"
#include "system/logging.h"
#include "system/passert.h"
#include "util/math.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gap_le_device_name.h>
#include <bluetooth/gatt.h>
#include <btutil/bt_device.h>

#include <inttypes.h>
#include <string.h>

static void prv_mtu_changed(const HcProtocol_GapServiceMtuChanged *evt) {
  GattDeviceMtuUpdateEvent mtu_resp = {
    .dev_address = evt->addr.address,
    .mtu = evt->mtu
  };
  bt_driver_cb_gatt_handle_mtu_update(&mtu_resp);
}

static void prv_handle_dev_name_response(const HcProtocol_GapDeviceNameResponseHeader *resp) {
  GAPLEConnection *connection = gap_le_connection_by_device(&resp->addr);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_INFO, "HcGap: no connection for "BT_DEVICE_ADDRESS_FMT,
            BT_DEVICE_ADDRESS_XPLODE(resp->addr.address));
    return;
  }

  // The value is a non-zero-terminated UTF-8 string
  int device_name_buffer_length = MIN(resp->name_length + 1, BT_DEVICE_NAME_BUFFER_SIZE);
  char *device_name = (char *)kernel_zalloc_check(device_name_buffer_length);
  if (!device_name) {
    return;
  }
  memcpy(device_name, resp->name, device_name_buffer_length - 1);

  if (connection->device_name) {
    kernel_free(connection->device_name);
  }
  connection->device_name = device_name;

  BTDeviceAddress *addr = kernel_zalloc_check(sizeof(BTDeviceAddress));
  memcpy(addr, &resp->addr, sizeof(BTDeviceAddress));
  system_task_add_callback(bt_driver_store_device_name_kernelbg_cb, addr);
}

void hc_endpoint_gap_service_resp_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageId_GapService_MtuChanged:
      prv_mtu_changed((const HcProtocol_GapServiceMtuChanged *)&msg->payload[0]);
      break;
    case HcMessageID_GapService_DeviceNameRequest:
      prv_handle_dev_name_response((const HcProtocol_GapDeviceNameResponseHeader *)msg->payload);
      break;
    default:
      PBL_LOG(LOG_LEVEL_INFO, "HcGap: unhandled message id: %d\n", msg->command_id);
      break;
  }
}

void hc_endpoint_gap_service_set_dev_name(const char *name) {
  hc_protocol_enqueue_with_payload(HcEndpointID_GapService, HcMessageID_GapService_SetName,
                                   (const uint8_t *)name, strlen(name) + 1);
}

void hc_endpoint_gap_service_device_name_request(const BTDeviceInternal *device) {
  hc_protocol_enqueue_with_payload(HcEndpointID_GapService,
                                   HcMessageID_GapService_DeviceNameRequest,
                                   (const uint8_t *)device,
                                   sizeof(BTDeviceInternal));
}

void hc_endpoint_gap_service_device_name_request_all(void) {
  hc_protocol_enqueue_with_payload(HcEndpointID_GapService,
                                   HcMessageID_GapService_DeviceNameRequest_All, NULL, 0);
}

void hc_endpoint_gap_service_set_local_address(bool allow_cycling,
                                               const BTDeviceAddress *pinned_address) {
  HcProtocol_GapServiceSetLocalAddress payload = {};
  payload.allow_cycling = allow_cycling;
  if (!allow_cycling) {
    PBL_ASSERTN(!bt_device_address_is_invalid(pinned_address));
    payload.pinned_addr = *pinned_address;
  }
  hc_protocol_enqueue_with_payload(HcEndpointID_GapService,
                                   HcMessageID_GapService_SetLocalAddress,
                                   (const uint8_t *)&payload, sizeof(payload));
}

bool hc_endpoint_gap_service_generate_private_resolvable_address(BTDeviceAddress *address_out) {
  bool success = false;
  const HcMessageID_GapService msg_id = HcMessageID_GapService_GeneratePrivateResolvable_address;
  HcProtocolMessage *response =
      hc_protocol_enqueue_with_payload_and_expect(HcEndpointID_GapService, msg_id, NULL, 0);
  if (response) {
    HcProtocol_GapServiceGeneratePrivateResolvableAddressResponse *payload =
        (HcProtocol_GapServiceGeneratePrivateResolvableAddressResponse *) response->payload;
    *address_out = payload->address;
    success = true;
  }
  kernel_free(response);
  return success;
}
