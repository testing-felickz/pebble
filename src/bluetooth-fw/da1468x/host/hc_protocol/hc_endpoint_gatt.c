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
#include "hc_protocol/hc_protocol.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gatt.h>
#include "kernel/pbl_malloc.h"
#include "system/logging.h"
#include "system/hexdump.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void hc_endpoint_gatt_handler_host(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Gatt_ReadCompleted: {
      HcGattReadRespData *r_data = (HcGattReadRespData *)&msg->payload[0];
      PBL_LOG(LOG_LEVEL_DEBUG, "Gatt: Got a read complete event: handle=%d, len=%d",
              r_data->att_handle, r_data->value_length);
      PBL_HEXDUMP(LOG_LEVEL_DEBUG, r_data->value, r_data->value_length);
      GattClientOpReadReponse r_resp = {
        .hdr = {
          .type = GattClientOpResponseRead,
          .error_code = r_data->status,
          .context = (void *)r_data->context_ref,
        },
        .value_length = r_data->value_length,
        .value = r_data->value,
      };
      bt_driver_cb_gatt_client_operations_handle_response(&r_resp.hdr);
      break;
    }
    case HcMessageID_Gatt_WriteCompleted: {
      HcGattWriteRespData *w_data = (HcGattWriteRespData *) &msg->payload[0];
      PBL_LOG(LOG_LEVEL_DEBUG, "Gatt: Got a write complete event: handle=%d", w_data->att_handle);
      GattClientOpWriteReponse w_resp = {
        .hdr = {
          .type = GattClientOpResponseWrite,
          .error_code = w_data->status,
          .context = (void *) w_data->context_ref,
        },
      };
      bt_driver_cb_gatt_client_operations_handle_response(&w_resp.hdr);
      break;
    }
    case HcMessageID_Gatt_Notification:
    case HcMessageID_Gatt_Indication: {
      HcGattNotifIndicData *ni_data = (HcGattNotifIndicData *)&msg->payload[0];
      GattServerNotifIndicEvent evt = {
        .dev_address = ni_data->hdr.addr.address,
        .attr_handle = ni_data->att_handle,
        .attr_val_len = ni_data->value_length,
        .attr_val = ni_data->value,
      };
      if (msg->command_id == HcMessageID_Gatt_Notification) {
        bt_driver_cb_gatt_handle_notification(&evt);
      } else {
        bt_driver_cb_gatt_handle_indication(&evt);
      }
      break;
    }
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unhandled command id: %d", msg->command_id);
  }
}

static BTErrno prv_enqueue_and_get_rv(
    HcCommandID cmd_id, const uint8_t *data, size_t data_len, bool resp_required) {
  BTErrno rv;
  if (resp_required) {
    HcProtocolMessage *resp = hc_protocol_enqueue_with_payload_and_expect(HcEndpointID_Gatt, cmd_id,
                                                                        data, data_len);

    rv = (resp != NULL) ? resp->payload[0] : BTErrnoPairingTimeOut;
    kernel_free(resp);
  } else {
    rv = hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, cmd_id, data, data_len) ?
        BTErrnoOK : BTErrnoNotEnoughResources;
  }
  return rv;
}

BTErrno hc_endpoint_gatt_read(const BTDeviceInternal *addr, uint16_t att_handle, void *context) {
  HcGattReadData data = {
    .hdr.addr = *addr,
    .context_ref = (uintptr_t)context,
    .att_handle = att_handle,
  };
  return prv_enqueue_and_get_rv(HcMessageID_Gatt_Read, (uint8_t *)&data, sizeof(data), true);
}

BTErrno hc_endpoint_gatt_write(const BTDeviceInternal *addr, uint16_t att_handle,
                               const uint8_t *value, uint16_t value_length,
                               bool resp_required, void *context) {
  const uint32_t alloc_size = sizeof(HcGattWriteData) + value_length;
  HcGattWriteData *data = kernel_malloc_check(alloc_size);
  *data = (HcGattWriteData) {
    .hdr.addr = *addr,
    .context_ref = (uintptr_t)context,
    .att_handle = att_handle,
    .value_length = value_length,
  };
  memcpy(&data->value[0], value, value_length);

  const HcCommandID cmd_id = (resp_required) ? HcMessageID_Gatt_Write
                                             : HcMessageID_Gatt_WriteNoResponse;
  BTErrno rv = prv_enqueue_and_get_rv(cmd_id, (uint8_t *)data, alloc_size, resp_required);
  kernel_free(data);
  return rv;
}
