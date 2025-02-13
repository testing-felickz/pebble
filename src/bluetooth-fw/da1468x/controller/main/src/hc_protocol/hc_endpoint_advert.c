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

#include "hc_protocol/hc_endpoint_advert.h"

#include "advert.h"
#include "system/logging.h"

// Dialog SDK:
#include "ble_common.h"

#include <bluetooth/bluetooth_types.h>

#include <inttypes.h>
#include <stdint.h>

static void prv_send_response(const HcProtocolMessage *msg, ble_error_t err) {
  hc_protocol_enqueue_response(msg, (uint8_t *)&err, sizeof(ble_error_t));
}

static void prv_handle_advert_enable(const HcProtocolMessage *msg) {
  HcAdvertEnableData *data = (HcAdvertEnableData *)msg->payload;

  // One slot is 625us:
  const uint16_t min_slots = data->min_interval_ms * 8 / 5;
  const uint16_t max_slots = data->max_interval_ms * 8 / 5;

//  PBL_LOG(LOG_LEVEL_DEBUG, "Advert; Setting min/max interval (ms) to %"PRIu16" / %"PRIu16,
//          data->min_interval_ms, data->max_interval_ms);

  HcAdvertEnableResponseData response_data = {};
  response_data.error = advert_set_interval(min_slots, max_slots);
  if (response_data.error == BLE_STATUS_OK) {
    response_data.current_state = advert_enable();
  }
  hc_protocol_enqueue_response(msg, (uint8_t *)&response_data, sizeof(response_data));
}

static void prv_handle_advert_disable(const HcProtocolMessage *msg) {
  advert_disable();
  prv_send_response(msg, BLE_STATUS_OK);
}

static void prv_handle_advert_set_adv_data(const HcProtocolMessage *msg) {
  advert_set_data((const BLEAdData *)&msg->payload[0]);
  prv_send_response(msg, BLE_STATUS_OK);
}

void hc_endpoint_advert_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Advert_Enable:
      prv_handle_advert_enable(msg);
      break;
    case HcMessageID_Advert_Disable:
      prv_handle_advert_disable(msg);
      break;
    case HcMessageID_Advert_SetAdvData:
      prv_handle_advert_set_adv_data(msg);
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcAdvert: unhandled message id: %d", msg->command_id);
  }
}
