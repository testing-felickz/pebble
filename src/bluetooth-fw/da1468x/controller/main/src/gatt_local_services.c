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

#include "gatt_local_services.h"

#include "dis_impl.h"
#include "hrm_impl.h"
#include "pebble_pairing_service_impl.h"
#include "ppogatt_emulated_server_wa.h"

#include "system/logging.h"

// Dialog SDK:
#include "attm_db.h"
#include "ble_common.h"
#include "ble_gap.h"
#include "ble_mgr.h"
#include "gapm_task.h"

#include <os/mutex.h>

#include <bluetooth/init.h>

// We'd like to avoid changing the ATT table between FW updates if we can. The implementation of
// the "Service Changed" GATT feature has been buggy on iOS in the past and in the Dialog BT driver
// lib side we still have to implement PBL-35626.
//
// The ATT table currently looks like this (R=Read, W=Write, I=Indicatable, N=Notifiable):
//
//  1 - Service: Generic Access Profile Service (UUID: 0x1800)
//  2   - Characteristic (R): Device Name
//  3     - Value
//  4   - Characteristic (R): Appearance
//  5     - Value
//  6   - Characteristic (R): Preferred Peripheral Connection Parameters
//  7     - Value
//
//  8 - Service: Generic Attribute Profile Service (UUID: 0x1801)
//  9   - Characteristic (RI): Service Changed
// 10     - Value
// 11     - CCCD
//
// 12 - Service: Device Information Service (UUID: 0x180A)
// 13   - Characteristic (R): Manufacturer Name
// 14     - Value
// 15   - Characteristic (R): Model Number
// 16     - Value
// 17   - Characteristic (R): Serial Number
// 18     - Value
// 19   - Characteristic (R): Firmware Revision
// 20     - Value
// 21   - Characteristic (R): Software Revision
// 22     - Value
//
// 23 - Service: Pebble Pairing Service (UUID: 0xFED9) -- Documentation here: http://pbl.io/gatt
// 24   - Characteristic (RN): Connectivity Status
// 25     - Value
// 26     - CCCD
// 27   - Characteristic (R): Trigger Pairing
// 28     - Value
// 29   - Characteristic (RWN): GATT MTU
// 30     - Value
// 31     - CCCD
// 32   - Characteristic (RWN): Connection Parameters
// 33     - Value
// 34     - CCCD
//
// 35 - Service: Heart Rate Monitor (UUID: 0x180D)
// 36   - Characteristic (N): Heart Rate Measurement (UUID: 0x2A37)
// 37     - Value
// 38     - CCCD
// 39   - Characteristic (R): Sensor Location (UUID: 0x2A38)
// 40     - Value
// 41   - Characteristic (W): Heart Rate Control Point (UUID: 0x2A39)
// 42     - Value
//
// 57344 (0xE000) - Service: PPoGATT Work-around Service (UUID: TBD)

static bool s_has_registered = false;
static PebbleMutex *s_svc_register_mutex;

void gatt_local_services_init(const BTDriverConfig *config) {
  s_svc_register_mutex = mutex_create();
  device_information_service_init(&config->dis_info);
  pebble_pairing_service_init();
  hrm_service_init(config->is_hrm_supported_and_enabled);
  ppogatt_service_init();
}

static void prv_gatt_local_services_register(void) {
  device_information_service_register(DEVICE_INFORMATION_SERVICE_EXPECTED_ATT_STARTING_HANDLE);
  pebble_pairing_service_register(PEBBLE_PAIRING_SERVICE_EXPECTED_ATT_STARTING_HANDLE);
  hrm_service_register(HRM_SERVICE_EXPECTED_ATT_STARTING_HANDLE);
  ppogatt_service_register(PEBBLE_PPOGATT_SERVICE_EXPECTED_ATT_STARTING_HANDLE);

  s_has_registered = true;
}

//! @note this will be called multiple times!
void gatt_local_services_register(void) {
  mutex_lock(s_svc_register_mutex);
  prv_gatt_local_services_register();
  mutex_unlock(s_svc_register_mutex);
}

void gatt_local_services_re_register_if_needed(void) {
  if (!s_has_registered) {
    // Avoid registering at this time, gatt_local_services_register() hasn't been called before.
    return;
  }
  mutex_lock(s_svc_register_mutex);

  // Under the hood all the API calls below can invoke the GAPM_SET_DEV_CONFIG_CMD. Within the RW
  // stack this will often result in an attmdb_destroy() call which nukes the internal attribute
  // database. It's hard to detect this happening aside from inspecting code paths (even an rv of
  // success or failure does not tell you whether or not a flush occurred. To work around this we
  // issue a call into a RW ROM function to see if our handle still exists. There isn't any locking
  // around this function but AFAICT only API calls made by this module would wind up updating this
  // internal list so I think its safe to call
  if (attmdb_get_service(PEBBLE_PAIRING_SERVICE_EXPECTED_ATT_STARTING_HANDLE) == NULL) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Service flush detected, re-registering ...");
    prv_gatt_local_services_register();
  }

  mutex_unlock(s_svc_register_mutex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// The functions below are wrappers for API calls that are known to clear out the ROM stack's
// ATT table. All the wrappers do is call the original API and then re-registering our services.

extern ble_error_t __real_ble_gap_address_set(const own_address_t *address, uint16_t renew_dur);
ble_error_t __wrap_ble_gap_address_set(const own_address_t *address, uint16_t renew_dur) {
  ble_error_t rv = __real_ble_gap_address_set(address, renew_dur);
  gatt_local_services_re_register_if_needed();
  return rv;
}

extern ble_error_t __real_ble_gap_device_name_set(const char *name, att_perm_t perm);
ble_error_t __wrap_ble_gap_device_name_set(const char *name, att_perm_t perm) {
  // Note: in spite of what the docstring says, the ATT database does not seem to get flushed by
  // calling ble_gap_device_name_set(), unless the write permissions have changed. We catch this
  // with our checks in gatt_local_services_re_register_if_needed

  ble_error_t rv = __real_ble_gap_device_name_set(name, perm);
  gatt_local_services_re_register_if_needed();

  return rv;
}

extern ble_error_t __real_ble_gap_appearance_set(gap_appearance_t appearance, att_perm_t perm);
ble_error_t __wrap_ble_gap_appearance_set(gap_appearance_t appearance, att_perm_t perm) {
  ble_error_t rv = __real_ble_gap_appearance_set(appearance, perm);
  gatt_local_services_re_register_if_needed();
  return rv;
}

extern ble_error_t __real_ble_gap_per_pref_conn_params_set(const gap_conn_params_t *conn_params);
ble_error_t __wrap_ble_gap_per_pref_conn_params_set(const gap_conn_params_t *conn_params) {
  ble_error_t rv = __real_ble_gap_per_pref_conn_params_set(conn_params);
  gatt_local_services_re_register_if_needed();
  return rv;
}

extern ble_error_t __real_ble_gap_role_set(const gap_role_t role);
ble_error_t __wrap_ble_gap_role_set(const gap_role_t role) {
  ble_error_t rv = __real_ble_gap_role_set(role);
  gatt_local_services_re_register_if_needed();
  return rv;
}

extern ble_error_t __real_ble_gap_mtu_size_set(uint16_t mtu_size);
ble_error_t __wrap_ble_gap_mtu_size_set(uint16_t mtu_size) {
  ble_error_t rv = __real_ble_gap_mtu_size_set(mtu_size);
  gatt_local_services_re_register_if_needed();
  return rv;
}
