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

#include "hc_protocol/hc_endpoint_discovery.h"

#include <bluetooth/gatt.h>
#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gatt_discovery.h>

#include <inttypes.h>

// -------------------------------------------------------------------------------------------------
// Gatt Client Discovery API calls

//! Assumes bt_lock is held
BTErrno bt_driver_gatt_start_discovery_range(
    const GAPLEConnection *connection, const ATTHandleRange *data) {
  return hc_endpoint_discovery_start(data, &connection->device) ? BTErrnoOK : BTErrnoOther;
}

//! Assumes bt_lock is held
BTErrno bt_driver_gatt_stop_discovery(GAPLEConnection *connection) {
  return hc_endpoint_discovery_stop(&connection->device) ? BTErrnoOK : BTErrnoOther;
}

void bt_driver_gatt_handle_discovery_abandoned(void) {
}
