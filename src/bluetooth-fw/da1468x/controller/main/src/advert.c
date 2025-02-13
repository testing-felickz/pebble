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

#include "advert.h"

#include "system/logging.h"
#include "system/passert.h"

#include "host_transport.h"

// Dialog SDK:
#include "ble_gap.h"

#include "FreeRTOS.h"
#include "light_mutex.h"
#include "semphr.h"

#include <bluetooth/bluetooth_types.h>
#include <os/mutex.h>

#include <inttypes.h>
#include <stdint.h>

extern void local_addr_handle_adverts_stopped(void);

// Accesses to these statics must be protected by prv_lock() calls
static AdvertState s_adv_state = AdvertState_Off;

static AdvertState s_desired_host_adv_state = AdvertState_Off;
static struct {
  bool needs_updating;
  BLEAdData ad_data;
  uint8_t data_buffer[2 * GAP_LE_AD_REPORT_DATA_MAX_LENGTH];
} s_desired_ad_data;

static PebbleRecursiveMutex *s_adv_mutex;

static void prv_lock(void) {
  mutex_lock_recursive(s_adv_mutex);
}

static void prv_unlock(void) {
  mutex_unlock_recursive(s_adv_mutex);
}

static gap_disc_mode_t prv_dialog_discoverable_type_for_flag(uint8_t flags) {
  if (flags & GAP_LE_AD_FLAGS_GEN_DISCOVERABLE_MASK) {
    return GAP_DISC_MODE_GEN_DISCOVERABLE;
  } else if (flags & GAP_LE_AD_FLAGS_LIM_DISCOVERABLE_MASK) {
    return GAP_DISC_MODE_LIM_DISCOVERABLE;
  } else {
    return GAP_DISC_MODE_NON_DISCOVERABLE;
  }
}

ble_error_t advert_set_interval(uint16_t min_slots, uint16_t max_slots) {
  // No need to lock / take here, this API just sets a couple vars in ble_mgr_dev_params,
  // while holding the ble_mgr_dev_params lock.
  ble_error_t rv = ble_gap_adv_intv_set(min_slots, max_slots);
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error: ble_gap_adv_intv_set - rv: %d", rv);
  }
  return rv;
}

static ble_error_t prv_set_mode_and_data(void) {
  const BLEAdData *ad_data = &s_desired_ad_data.ad_data;
  // FIXME PBL-34428: We are chopping off the beginning of the advertisement data because that is
  // where the flags are stored. We parse the flags and translate to a specific type of discovery
  // mode required by the Dialog part. Do some error checking while we are at it.
  const uint8_t *adv_bytes = ad_data->data;
  size_t adv_bytes_len = ad_data->ad_data_length;
  gap_disc_mode_t disc_mode = GAP_DISC_MODE_NON_DISCOVERABLE;

  // Make sure the whole packet data is at least 3 bytes.
  // Make sure that the length of the first field is at least 2 (contains the type and flags byte)
  // Make sure that the type is GAP_DATA_TYPE_FLAGS
  if ((ad_data->ad_data_length >= 3)
      && (*adv_bytes >= 2)
      && *(adv_bytes + 1) == GAP_DATA_TYPE_FLAGS) {
    // Start the iter at the flags byte
    const uint8_t *iter = adv_bytes + 2;
    disc_mode = prv_dialog_discoverable_type_for_flag(*iter);

    // Move iter past the flags
    iter += 1;

    adv_bytes = iter;
    adv_bytes_len -= 3;
  }

  ble_error_t rv = ble_gap_adv_mode_set(disc_mode);
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error: ble_gap_adv_mode_set - rv: %d", rv);
    // Not returning, probably better to try to advertise regardless of this error.
  }

  rv = ble_gap_adv_data_set(adv_bytes_len, adv_bytes,
                            ad_data->scan_resp_data_length,
                            &ad_data->data[ad_data->ad_data_length]);
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Error: ble_gap_adv_data_set - rv: %d", rv);
  }

  s_desired_ad_data.needs_updating = false;
  return rv;
}

static ble_error_t prv_advert_enable(void) {
  ble_error_t rv = BLE_ERROR_FAILED;
  if (s_desired_ad_data.needs_updating) {
    rv = prv_set_mode_and_data();
    if (rv != BLE_STATUS_OK) {
      return rv;
    }
  }

  rv = ble_gap_adv_start(GAP_CONN_MODE_UNDIRECTED);
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_adv_start: status = 0x%x", rv);
  } else {
    PBL_LOG(LOG_LEVEL_DEBUG, "Adverts (re)started");
  }
  return rv;
}

static ble_error_t prv_advert_disable(void) {
  ble_error_t rv = ble_gap_adv_stop();
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_adv_stop: status = 0x%x", rv);
  }
  return rv;
}

static ble_error_t prv_try_to_enter_host_desired_state(void) {
  ble_error_t rv = BLE_STATUS_OK;
  prv_lock();
  {
    // The only valid desired host states are Off & Running
    switch (s_desired_host_adv_state) {
      case AdvertState_Running:
      case AdvertState_Off:
        break;
      default:
        WTF;
    }

    // Are we already in the state we desire
    if (s_desired_host_adv_state == s_adv_state) {
      rv = BLE_ERROR_ALREADY_DONE;
      goto unlock;
    }


    switch (s_adv_state) {
      case AdvertState_Pausing:
      case AdvertState_Paused:
      case AdvertState_Stopping:
        // We are transitioning, these are handled by other routines
        goto unlock;
      default:
        break;
    }

    if (s_desired_host_adv_state == AdvertState_Running) {
      // TODO: Does the dialog stack gracefully handle calling stop before adds
      // are truly running. Is there an event we can wait on and have a Starting state?
      s_adv_state = AdvertState_Running;
      rv = prv_advert_enable();

    } else if (s_desired_host_adv_state == AdvertState_Off) {
      s_adv_state = AdvertState_Stopping;
      rv = prv_advert_disable();
    }
  }
unlock:
  prv_unlock();
  return rv;
}

AdvertState advert_enable(void) {
  AdvertState rv;
  prv_lock();
  {
    s_desired_host_adv_state = AdvertState_Running;
    prv_try_to_enter_host_desired_state();
    rv = s_adv_state;
  }
  prv_unlock();
  return rv;
}

void advert_disable(void) {
  prv_lock();
  {
    s_desired_host_adv_state = AdvertState_Off;
    prv_try_to_enter_host_desired_state();
  }
  prv_unlock();
}

void advert_set_data(const BLEAdData *ad_data) {
  prv_lock();
  {
    memcpy(&s_desired_ad_data.ad_data, ad_data,
           sizeof(BLEAdData) + ad_data->ad_data_length + ad_data->scan_resp_data_length);
    s_desired_ad_data.needs_updating = true;
  }
  prv_unlock();
}

static void prv_resume_if_needed(void) {
  if (s_desired_host_adv_state == AdvertState_Running) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Unpausing advertisements");
  }
  s_adv_state = AdvertState_Off;
  prv_try_to_enter_host_desired_state();
}

void advert_handle_completed(const ble_evt_gap_adv_completed_t *evt) {
  prv_lock();
  {
    PBL_LOG(LOG_LEVEL_DEBUG, "advert_handle_completed %"PRIu8, evt->status);
    if (s_adv_state == AdvertState_Pausing) {
      s_adv_state = AdvertState_Paused;
    } else {
      s_adv_state = AdvertState_Off;
    }

    // Note: Ideally, the controller would inform us why the advertisement had stopped. For
    // example, if we knew that the reason was due to a slave device connecting we would not
    // restart advertisements. However, I don't see a robust way to conclude this based on the
    // events the ROM stack emits.
    prv_try_to_enter_host_desired_state();
  }
  prv_unlock();

  local_addr_handle_adverts_stopped();

  prv_lock();
  {
    // Make sure we never stay paused once local_addr has a chance to do whatever it needs to do
    // while advertisements were paused.
    if (s_adv_state == AdvertState_Paused) {
      prv_resume_if_needed();
    }
  }
  prv_unlock();
}

//! @note This is here exclusively for local_addr.c!
//! If you also need to use advert_pause/resume, it's time to add a refcount.
bool advert_execute_cb_if_adverts_are_paused(void (*cb)(void *), void *ctx) {
  bool cb_executed = false;
  prv_lock();
  {
    switch (s_adv_state) {
      // States where we can immediately transition to paused
      case AdvertState_Off:
      case AdvertState_Paused:
        s_adv_state = AdvertState_Paused;
        break;
      default:
        break;
    }

    if (s_adv_state != AdvertState_Paused) {
      PBL_LOG(LOG_LEVEL_DEBUG, "Pausing advertisements, state = 0x%x",
             s_adv_state);
      if (s_adv_state == AdvertState_Running) {
        prv_advert_disable();
      }

      s_adv_state = AdvertState_Pausing;
    } else if (s_adv_state == AdvertState_Paused) {
      // It's safe to call the cb
      cb(ctx);
      cb_executed = true;
      prv_resume_if_needed();
    }
  }
  prv_unlock();
  return cb_executed;
}

void advert_init(void) {
  s_adv_state = AdvertState_Off;
  s_desired_host_adv_state = AdvertState_Off;
  s_desired_ad_data.needs_updating = false;
  s_desired_ad_data.ad_data.ad_data_length = 0;
  s_desired_ad_data.ad_data.scan_resp_data_length = 0;
  s_adv_mutex = mutex_create_recursive();
}
