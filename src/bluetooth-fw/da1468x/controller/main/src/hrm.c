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

#include "hrm_impl.h"

#include "connection.h"
#include "dialog_utils.h"
#include "gatt_local_services.h"
#include "service_changed.h"
#include "system/logging.h"
#include "system/passert.h"
#include "hc_protocol/hc_endpoint_hrm.h"

// Dialog SDK:
#include "hrs.h"
#include "osal.h"

#include <btutil/bt_device.h>
#include <util/size.h>

static bool s_is_hrm_enabled = false;
static hr_service_t s_hrs;

void hrm_service_init(bool is_hrm_supported_and_enabled) {
  s_is_hrm_enabled = is_hrm_supported_and_enabled;
  ble_service_add(&s_hrs.svc);
}

static void prv_handle_subscribe(uint16_t conn_idx, bool enabled) {
  Connection *connection = connection_by_idx(conn_idx);
  if (!connection) {
    return;
  }

  HcHrmSubscription subscription;
  subscription.is_subscribed = enabled;
  connection_get_address(connection, &subscription.device);
  hc_endpoint_hrm_update_subscription(&subscription);
}

void hrm_service_register(uint16_t start_hdl) {
  if (!s_is_hrm_enabled) {
    // Set all handlers to NULL, so the ble_service dispatcher won't ever try to call us:
    s_hrs.svc = (const ble_service_t) {};
    return;
  }

  const hrs_body_sensor_location_t sensor_location = HRS_SENSOR_LOC_WRIST;
  static const hrs_callbacks_t s_callbacks = {
    .ee_reset = NULL, // Beat-to-beat interval data is not supported at the moment.
    .notif_changed = prv_handle_subscribe,
  };
  hrs_init(&s_hrs, sensor_location, &s_callbacks, start_hdl);
  PBL_ASSERTN(start_hdl == s_hrs.svc.start_h);
}

void hrm_service_handle_measurement(const BleHrmServiceMeasurement *measurement,
                                    const BTDeviceInternal *permitted_devices,
                                    size_t num_permitted_devices) {
  const hrs_measurement_t hrs_measurement = {
    .bpm = measurement->bpm,
    .contact_supported = true,
    .contact_detected = measurement->is_on_wrist,
    .has_energy_expended = false,
    // NTH: Use calories burnt calculation from the activity algo to set energy_expended.
    // https://pebbletechnology.atlassian.net/browse/PBL-42867
    .energy_expended = 0,
    .rr_num = 0,
  };

  // Only notify permitted devices:
  gap_device_t devices[8];
  size_t length = ARRAY_LENGTH(devices);
  ble_gap_get_devices(GAP_DEVICE_FILTER_CONNECTED, NULL, &length, devices);
  for (int i = 0; i < (int)length; ++i) {
    const gap_device_t *const gap_device = &devices[i];
    BTDeviceInternal device;
    dialog_utils_bd_address_to_bt_device(&gap_device->address, &device);
    for (int j = 0; j < (int)num_permitted_devices; ++j) {
      if (bt_device_internal_equal(&device, &permitted_devices[j])) {
        hrs_notify_measurement(&s_hrs.svc, gap_device->conn_idx, &hrs_measurement);
      }
    }
  }
}

void hrm_service_handle_enable(bool enable) {
  if (s_is_hrm_enabled == enable) {
    return;
  }
  PBL_LOG(LOG_LEVEL_DEBUG, "hrm_service_handle_enable %u", enable);
  s_is_hrm_enabled = enable;

  if (enable) {
    hrm_service_register(HRM_SERVICE_EXPECTED_ATT_STARTING_HANDLE);
    service_changed_send_indication_to_all(HRM_SERVICE_EXPECTED_ATT_STARTING_HANDLE,
                                           HRM_SERVICE_EXPECTED_ATT_ENDING_HANDLE);
  } else {
    // Unfortunately, there is no clean way to remove a service from RivieraWaves once it has been
    // added. The various ble_... APIs do nuke the GATT DB entirely, but can only be called when
    // there is no connection / on-going air operations.
    // The next time the BT stack is restarted (granted the user pref hasn't changed to "enabled"),
    // the service will be gone.
  }
}
