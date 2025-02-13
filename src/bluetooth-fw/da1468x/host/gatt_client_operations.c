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

#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth_types.h>
#include <btutil/bt_device.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

BTErrno bt_driver_gatt_write_without_response(GAPLEConnection *connection,
                                              const uint8_t *value,
                                              size_t value_length,
                                              uint16_t att_handle) {
  BTDeviceInternal *device = &connection->device;
  const bool resp_required = false;
  // FIXME PBL-34465: Need to translate between Dialog errors and BTErrno
  int16_t ret_val = hc_endpoint_gatt_write(device, att_handle, value, value_length,
                                           resp_required, NULL);
  return (BTErrno)ret_val;
}

BTErrno bt_driver_gatt_write(GAPLEConnection *connection,
                             const uint8_t *value,
                             size_t value_length,
                             uint16_t att_handle,
                             void *context) {
  BTDeviceInternal *device = &connection->device;
  const bool resp_required = true;
  // FIXME PBL-34465: Need to translate between Dialog errors and BTErrno
  int16_t ret_val = hc_endpoint_gatt_write(device, att_handle, value, value_length,
                                           resp_required, context);
  return (BTErrno)ret_val;
}

BTErrno bt_driver_gatt_read(GAPLEConnection *connection,
                            uint16_t att_handle,
                            void *context) {
  BTDeviceInternal *device = &connection->device;
  // FIXME PBL-34465: Need to translate between Dialog errors and BTErrno
  int16_t ret_val = hc_endpoint_gatt_read(device, att_handle, context);
  return (BTErrno)ret_val;
}
