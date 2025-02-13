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

#include <bluetooth/bt_driver_advert.h>
#include <bluetooth/bluetooth_types.h>

#include <string.h>
#include <inttypes.h>

#include "advert_state.h"
#include "hc_protocol/hc_endpoint_advert.h"
#include "hc_protocol/hc_protocol.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include "ble_common.h"

static bool prv_finish_request_response(HcProtocolMessage *request, HcProtocolMessage *resp,
                                        bool should_free_request) {
  bool rv = false;
  ble_error_t error = BLE_ERROR_FAILED;
  if (resp) {
    if (request->command_id == HcMessageID_Advert_Enable) {
      const HcAdvertEnableResponseData *data = (const HcAdvertEnableResponseData *)resp->payload;
      error = data->error;
      if (data->current_state != AdvertState_Running) {
        PBL_LOG(LOG_LEVEL_INFO, "State was 0x%x after advert enable", data->current_state);
      }
    } else {
      const ble_error_t *error_ptr = (const ble_error_t *)resp->payload;
      error = *error_ptr;
    }
    if (error == BLE_STATUS_OK) {
      rv = true;
    } else {
      PBL_LOG(LOG_LEVEL_ERROR, "Advert error: 0x%"PRIx8" for cmd: 0x%"PRIx8,
              error, request->command_id);
    }
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "Advert timeout for cmd: 0x%"PRIx8, request->command_id);
  }
  if (should_free_request) {
    kernel_free(request);
  }
  kernel_free(resp);
  return rv;
}

void bt_driver_advert_advertising_disable(void) {
  HcProtocolMessage request = {
    .message_length = sizeof(request),
    .endpoint_id = HcEndpointID_Advert,
    .command_id = HcMessageID_Advert_Disable,
  };
  HcProtocolMessage *resp = hc_protocol_enqueue_and_expect(&request);
  prv_finish_request_response(&request, resp, false /* should_free_request */);
}

bool bt_driver_advert_client_get_tx_power(int8_t *tx_power) {
  // TODO PBL-34354: Must use ble_read_tx_power, but we don't know the connection index here.
  // Will have to figure out how to store and keep track of them.
  *tx_power = 0;
  return true;
}

void bt_driver_advert_set_advertising_data(const BLEAdData *ad_data) {
  const uint32_t ad_data_len =
      sizeof(BLEAdData) + ad_data->ad_data_length + ad_data->scan_resp_data_length;

  const uint32_t alloc_size = sizeof(HcProtocolMessage) + ad_data_len;
  HcProtocolMessage *request = kernel_malloc_check(alloc_size);
  *request = (HcProtocolMessage) {
    .message_length = alloc_size,
    .endpoint_id = HcEndpointID_Advert,
    .command_id = HcMessageID_Advert_SetAdvData,
  };
  memcpy(request->payload, ad_data, ad_data_len);

  HcProtocolMessage *resp = hc_protocol_enqueue_and_expect(request);
  prv_finish_request_response(request, resp, true /* should_free_request */);
}

bool bt_driver_advert_advertising_enable(uint32_t min_interval_ms, uint32_t max_interval_ms,
                                         bool enable_scan_resp) {
  const uint32_t alloc_size = sizeof(HcProtocolMessage) + sizeof(HcAdvertEnableData);
  HcProtocolMessage *request = kernel_malloc_check(alloc_size);
  *request = (HcProtocolMessage) {
    .message_length = alloc_size,
    .endpoint_id = HcEndpointID_Advert,
    .command_id = HcMessageID_Advert_Enable,
  };
  HcAdvertEnableData *request_data = (HcAdvertEnableData *)&request->payload[0];
  *request_data = (HcAdvertEnableData) {
    .min_interval_ms = min_interval_ms,
    .max_interval_ms = max_interval_ms,
  };

  HcProtocolMessage *resp = hc_protocol_enqueue_and_expect(request);
  return prv_finish_request_response(request, resp, true /* should_free_request */);
}

// These are essentially stubs that were needed for bugs in another Bluetooth stack.
bool bt_driver_advert_is_connectable(void) {
  return true;
}

bool bt_driver_advert_client_has_cycled(void) {
  return true;
}

void bt_driver_advert_client_set_cycled(bool has_cycled) {
  // nothing
}

bool bt_driver_advert_should_not_cycle(void) {
  return false;
}
