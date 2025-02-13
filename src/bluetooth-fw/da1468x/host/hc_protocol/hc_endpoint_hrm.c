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

#include "kernel/pbl_malloc.h"

#include <bluetooth/hrm_service.h>

#include <string.h>

void hc_endpoint_hrm_handler(const HcProtocolMessage *msg) {
#if !RECOVERY_FW && CAPABILITY_HAS_BUILTIN_HRM
  switch (msg->command_id) {
    case HcMessageID_HRM_UpdateSubscription: {
      const HcHrmSubscription *subscription = (const HcHrmSubscription *)msg->payload;
      bt_driver_cb_hrm_service_update_subscription(&subscription->device,
                                                   subscription->is_subscribed);
      break;
    }

    default:
      break;
  }
#endif
}

void hc_endpoint_hrm_send_measurement(const BleHrmServiceMeasurement *measurement,
                                      const BTDeviceInternal *permitted_devices,
                                      size_t num_permitted_devices) {
#if !RECOVERY_FW && CAPABILITY_HAS_BUILTIN_HRM
  const size_t msg_size = (sizeof(HcProtocolMessage) + sizeof(HcHrmMeasurement) +
                           (num_permitted_devices * sizeof(BTDeviceInternal)));
  HcProtocolMessage *const msg = kernel_zalloc_check(msg_size);
  msg->message_length = msg_size;
  msg->endpoint_id = HcEndpointID_HRM;
  msg->command_id = HcMessageID_HRM_Measurement;
  HcHrmMeasurement *const payload = (HcHrmMeasurement *) msg->payload;
  payload->bpm = measurement->bpm;
  payload->is_on_wrist = measurement->is_on_wrist;
  payload->num_devices = num_permitted_devices;
  memcpy(payload->devices, permitted_devices, sizeof(BTDeviceInternal) * num_permitted_devices);
  hc_protocol_enqueue(msg);
  kernel_free(msg);
#endif
}


void hc_endpoint_hrm_enable(bool enable) {
#if !RECOVERY_FW
  const HcHrmEnableCmd enable_cmd = {
    .enable = enable,
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_HRM, HcMessageID_HRM_Enable,
                                   (const uint8_t *)&enable_cmd, sizeof(enable_cmd));
#endif
}
