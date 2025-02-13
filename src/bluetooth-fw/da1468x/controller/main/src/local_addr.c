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

#include "local_addr_impl.h"
#include "ble_task.h"
#include "advert.h"
#include "system/logging.h"
#include "system/passert.h"

// Dialog SDK:
#include "ble_common.h"
#include "ble_gap.h"
#include "ble_irb_helper.h"
#include "ble_mgr.h"
#include "osal.h"

#include <bluetooth/bluetooth_types.h>

#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

extern bool advert_execute_cb_if_adverts_are_paused(void (*cb)(void *), void *ctx);

#define LOCAL_ADDR_RENEW_DURATION_SECS (600)  // Cycle every 600s or 10mins

typedef enum {
  LocalAddressState_Uninitialized,
  LocalAddressState_AwaitingAdvertisementsPaused,
  LocalAddressState_AwaitingDisconnection,
  LocalAddressState_InSync,
} LocalAddressState;

typedef struct {
  bool allow_cycling;
  BTDeviceAddress pinned_address;
} LocalAddressPolicy;

// These statics should only get access from ble_task, so no locking needed.
static LocalAddressPolicy s_local_addr_desired_policy;
static LocalAddressState s_local_addr_state;

static void prv_get_desired_own_address(own_address_t *desired_address_out) {
  if (s_local_addr_desired_policy.allow_cycling) {
    PBL_LOG(LOG_LEVEL_DEBUG,
            "Trying to set local address to auto-cycling private resolvable address!");
    desired_address_out->addr_type = PRIVATE_RANDOM_RESOLVABLE_ADDRESS;
    return;
  }

  PBL_LOG(LOG_LEVEL_DEBUG,
          "Trying to set local address: allow_cycling=%d, pinned_address="BT_DEVICE_ADDRESS_FMT,
          s_local_addr_desired_policy.allow_cycling,
          BT_DEVICE_ADDRESS_XPLODE(s_local_addr_desired_policy.pinned_address));

  // Note: the pinned address is actually resolvable, but we need to lie to the SDK here to avoid
  // automatically cycling, see irb_ble_handler_gap_address_set_cmd():
  desired_address_out->addr_type = PRIVATE_STATIC_ADDRESS;
  memcpy(&desired_address_out->addr,
         &s_local_addr_desired_policy.pinned_address.octets, sizeof(desired_address_out->addr));
}

typedef struct {
  own_address_t desired_own_address;
  bool did_succeed_setting_own_address;
} OwnAddressUpdateInfo;

static void prv_try_set_own_address_cb(void *ctx) {
  OwnAddressUpdateInfo *info = ctx;
  // Note: renew param is not used when addr_type is "static address":
  uint16_t renew_duration_10ms_steps = LOCAL_ADDR_RENEW_DURATION_SECS * 100 /* 10ms steps */;
  ble_error_t e = ble_gap_address_set(&info->desired_own_address, renew_duration_10ms_steps);
  if (e == BLE_STATUS_OK) {
    info->did_succeed_setting_own_address = true;
  } else {
    // This is possible if there is still a connection. We could try checking if there are any
    // connections at the moment, but this is prone to races because of the various queues that
    // are used. Instead, just try ble_gap_address_set() and see if it was successful.
    PBL_LOG(LOG_LEVEL_DEBUG,
            "Address couldn't be updated (e=0x%x). Will try again later.", e);
    info->did_succeed_setting_own_address = false;
  }
}

static bool prv_current_address_is_already_pinned_address(void) {
  if (s_local_addr_desired_policy.allow_cycling) {
    // Pinned address isn't used.
    return false;
  }
  own_address_t own_addr = {};
  ble_gap_address_get(&own_addr);
  if (own_addr.addr_type != PRIVATE_STATIC_ADDRESS) {
    return false;
  }
  return (0 == memcmp(own_addr.addr, s_local_addr_desired_policy.pinned_address.octets,
                      sizeof(own_addr.addr)));
}

static void prv_try_updating_own_address(void) {
  OwnAddressUpdateInfo update_info = {};

  prv_get_desired_own_address(&update_info.desired_own_address);

  if (!prv_current_address_is_already_pinned_address()) {
    // FIXME - PBL-36339: Pause scanning too when we start using it for BLE Central APIs
    if (!advert_execute_cb_if_adverts_are_paused(prv_try_set_own_address_cb, &update_info)) {
      // Wait for local_addr_handle_disconnection() to happen.
      s_local_addr_state = LocalAddressState_AwaitingAdvertisementsPaused;
      return;
    }

    if (!update_info.did_succeed_setting_own_address) {
      // Wait for local_addr_handle_disconnection() to happen.
      s_local_addr_state = LocalAddressState_AwaitingDisconnection;
      return;
    }
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Local address policy in sync!");
  s_local_addr_state = LocalAddressState_InSync;
}

// @note: Everything below executes on ble_task to avoid locking.
static void prv_assert_is_executing_on_ble_task(void) {
  ble_task_assert_is_executing_on_ble_task();
}

void local_addr_set(bool allow_cycling, const BTDeviceAddress *pinned_address) {
  prv_assert_is_executing_on_ble_task();

  s_local_addr_desired_policy.allow_cycling = allow_cycling;
  if (!allow_cycling) {
    s_local_addr_desired_policy.pinned_address = *pinned_address;
  } else {
    s_local_addr_desired_policy.pinned_address = (BTDeviceAddress) {};
  }
  prv_try_updating_own_address();
}

void local_addr_handle_update(const BTDeviceAddress *updated_address) {
  prv_assert_is_executing_on_ble_task();

  if (s_local_addr_state == LocalAddressState_InSync &&
      (!s_local_addr_desired_policy.allow_cycling)) {
    PBL_LOG(LOG_LEVEL_ERROR,
            "Address cycled even though it was expected not to! " BT_DEVICE_ADDRESS_FMT,
            BT_DEVICE_ADDRESS_XPLODE_PTR(updated_address));
  }
}

void local_addr_handle_adverts_stopped(void) {
  prv_assert_is_executing_on_ble_task();

  if (s_local_addr_state == LocalAddressState_AwaitingAdvertisementsPaused) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Adverts paused, trying to update own address");
    prv_try_updating_own_address();
  }
}

void local_addr_handle_disconnection(void) {
  prv_assert_is_executing_on_ble_task();

  if (s_local_addr_state == LocalAddressState_AwaitingDisconnection) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Got disconnection, trying to update own address");
    prv_try_updating_own_address();
  }
}

void local_addr_init(void) {
  s_local_addr_state = LocalAddressState_Uninitialized;

  // Put all zeroes into ble_dev_params.own_address, as a known starting state.
  // The all zeroes address is used in prv_get_desired_own_address(), to check whether a resolvable
  // address has been generated yet or not.
  const own_address_t init_own_address = {
    .addr_type = PRIVATE_RANDOM_RESOLVABLE_ADDRESS,
    .addr = {},
  };
  ble_gap_address_set(&init_own_address, LOCAL_ADDR_RENEW_DURATION_SECS);
}
