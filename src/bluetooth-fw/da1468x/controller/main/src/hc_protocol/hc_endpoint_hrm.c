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

#include "hc_protocol/hc_endpoint_hrm.h"
#include "hc_protocol/hc_protocol.h"

#include "hrm_impl.h"

static void prv_handle_measurement_msg(const HcProtocolMessage *msg) {
  const HcHrmMeasurement *hc_hrm_measurement = (const HcHrmMeasurement *)msg->payload;

  const BleHrmServiceMeasurement hrm_measurement = {
    .bpm = hc_hrm_measurement->bpm,
    .is_on_wrist = hc_hrm_measurement->is_on_wrist,
  };
  hrm_service_handle_measurement(&hrm_measurement, hc_hrm_measurement->devices,
                                 hc_hrm_measurement->num_devices);
}

void hc_endpoint_hrm_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_HRM_Measurement:
      prv_handle_measurement_msg(msg);
      break;

    case HcMessageID_HRM_Enable:
      hrm_service_handle_enable(((HcHrmEnableCmd *)msg)->enable);
      break;

    default:
      break;
  }
}

void hc_endpoint_hrm_update_subscription(const HcHrmSubscription *subscription) {
  hc_protocol_enqueue_with_payload(HcEndpointID_HRM, HcMessageID_HRM_UpdateSubscription,
                                   (const uint8_t *)subscription, sizeof(*subscription));
}
