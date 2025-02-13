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

#include <bluetooth/hrm_service.h>

#include "kernel/pbl_malloc.h"

#include "hc_protocol/hc_endpoint_hrm.h"

bool bt_driver_is_hrm_service_supported(void) {
  return true;
}

void bt_driver_hrm_service_enable(bool enable) {
  hc_endpoint_hrm_enable(enable);
}

void bt_driver_hrm_service_handle_measurement(const BleHrmServiceMeasurement *measurement,
                                              const BTDeviceInternal *permitted_devices,
                                              size_t num_permitted_devices) {
  hc_endpoint_hrm_send_measurement(measurement, permitted_devices, num_permitted_devices);
}
