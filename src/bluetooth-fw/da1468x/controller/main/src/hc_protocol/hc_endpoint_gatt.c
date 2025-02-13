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

#include "hc_protocol/hc_endpoint_gatt.h"

#include "connection.h"
#include "gatt_wrapper.h"
#include "hc_protocol/hc_protocol.h"
#include "kernel/pbl_malloc.h"
#include "ppogatt_emulated_server_wa.h"
#include "system/logging.h"

#include <bluetooth/bluetooth_types.h>

#include <stdint.h>
#include <string.h>

void hc_endpoint_gatt_handler_controller(const HcProtocolMessage *msg) {
  BTErrno rv = BTErrnoOK;

  HcGattHdr *hdr = (HcGattHdr *)&msg->payload[0];
  Connection *conn = connection_by_address(&hdr->addr);
  if (!conn) {
    rv = BTErrnoInvalidParameter;
    goto respond;
  }

  const uint16_t conn_idx = connection_get_idx(conn);
  if (ppogatt_emulated_server_handle_msg(conn_idx, conn, msg)) {
    return;
  }

  const GattReqSource req_source = GattReqSourceHost;

  switch (msg->command_id) {
    case HcMessageID_Gatt_Read: {
      HcGattReadData *r_data = (HcGattReadData *)hdr;
      gatt_wrapper_read(conn_idx, r_data->att_handle, r_data->context_ref, req_source);
      break;
    }
    case HcMessageID_Gatt_Write: {
      HcGattWriteData *w_data = (HcGattWriteData *)hdr;
      gatt_wrapper_write(conn_idx, w_data->att_handle, w_data->value_length, w_data->value,
                         w_data->context_ref, req_source);
      break;
    }
    case HcMessageID_Gatt_WriteNoResponse: {
      HcGattWriteData *w_data = (HcGattWriteData *)hdr;
      gatt_wrapper_write_no_resp(conn_idx, w_data->att_handle, w_data->value_length, w_data->value);
      return; // Don't respond
    }
  }

  rv = BTErrnoOK;

respond:
  hc_protocol_enqueue_response(msg, (uint8_t *)&rv, sizeof(rv));
}

void hc_endpoint_gatt_send_read_complete(const BTDeviceInternal *addr, uint16_t handle,
    BLEGATTError status, uint16_t value_length, const uint8_t *value, uintptr_t context_ref) {

  const uint32_t alloc_size = sizeof(HcGattReadRespData) + value_length;
  HcGattReadRespData *data = kernel_zalloc_check(alloc_size);
  *data = (HcGattReadRespData) {
    .status = status,
    .hdr.addr = *addr,
    .att_handle = handle,
    .value_length = value_length,
    .context_ref = context_ref,
  };
  memcpy(data->value, value, value_length);
  hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, HcMessageID_Gatt_ReadCompleted,
                                   (uint8_t *)data, alloc_size);
  kernel_free(data);
}

void hc_endpoint_gatt_send_write_complete(const BTDeviceInternal *addr, uint16_t handle,
                                          BLEGATTError status, uintptr_t context_ref) {
  const uint32_t alloc_size = sizeof(HcGattWriteRespData);
  HcGattWriteRespData *data = kernel_zalloc_check(alloc_size);
  *data = (HcGattWriteRespData) {
    .status = status,
    .hdr.addr = *addr,
    .att_handle = handle,
    .context_ref = context_ref,
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, HcMessageID_Gatt_WriteCompleted,
                                   (uint8_t *)data, alloc_size);
  kernel_free(data);
}

static void prv_send_notification_indication(const BTDeviceInternal *addr, uint16_t handle,
                                             uint16_t value_length, const uint8_t *value,
                                             HcMessageID_Gatt msg_id) {
  const uint32_t alloc_size = sizeof(HcGattNotifIndicData) + value_length;
  HcGattNotifIndicData *data = kernel_zalloc_check(alloc_size);
  *data = (HcGattNotifIndicData) {
    .hdr.addr = *addr,
    .att_handle = handle,
    .value_length = value_length,
  };
  memcpy(data->value, value, value_length);
  hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, msg_id, (uint8_t *)data, alloc_size);
  kernel_free(data);
}

void hc_endpoint_gatt_send_notification(const BTDeviceInternal *addr, uint16_t handle,
                                        uint16_t value_length, const uint8_t *value) {
  prv_send_notification_indication(addr, handle, value_length, value,
                                   HcMessageID_Gatt_Notification);
}

void hc_endpoint_gatt_send_indication(const BTDeviceInternal *addr, uint16_t handle,
                                      uint16_t value_length, const uint8_t *value) {
  prv_send_notification_indication(addr, handle, value_length, value,
                                   HcMessageID_Gatt_Indication);
}
