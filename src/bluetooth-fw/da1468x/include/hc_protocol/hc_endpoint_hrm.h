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

#pragma once

#include "hc_protocol/hc_protocol.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/hrm_service.h>
#include <util/attributes.h>

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HcMessageID_HRM_UpdateSubscription = 0x1,
  HcMessageID_HRM_Measurement = 0x2,
  HcMessageID_HRM_Enable = 0x3,
} HcMessageID_HRM;

typedef struct PACKED {
  BTDeviceInternal device;
  bool is_subscribed;
} HcHrmSubscription;

typedef struct PACKED {
  uint16_t bpm;
  bool is_on_wrist:1;

  //! Array of devices (that have been granted access) to which the update should be sent:
  uint32_t num_devices;
  BTDeviceInternal devices[];
} HcHrmMeasurement;

typedef struct PACKED {
  bool enable:1;
} HcHrmEnableCmd;

void hc_endpoint_hrm_handler(const HcProtocolMessage *msg);

//! Host -> Controller
void hc_endpoint_hrm_send_measurement(const BleHrmServiceMeasurement *measurement,
                                      const BTDeviceInternal *permitted_devices,
                                      size_t num_permitted_devices);

//! Host -> Controller
void hc_endpoint_hrm_enable(bool enable);

//! Controller -> Host
void hc_endpoint_hrm_update_subscription(const HcHrmSubscription *subscription);
