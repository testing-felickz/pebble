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

#include "comm/ble/gap_le_advert.h"

#include <bluetooth/adv_reconnect.h>
#include <util/size.h>

const GAPLEAdvertisingJobTerm *bt_driver_adv_reconnect_get_job_terms(size_t *num_terms_out) {
  static const GAPLEAdvertisingJobTerm s_advert_terms[] = {
    // When starting to advertise for reconnection, burst for 25 seconds:
    [0] = {
      .duration_secs = 25,
      .min_interval_slots = 244, // 152.5 ms -- Apple-recommended advertising interval
      .max_interval_slots = 256, // 160.0 ms
    },

    // For the remainder of the advertising for reconnection, cycle between 5 second burst and
    // 20 seconds of low-duty cycle advertising. The Dialog chip is pretty power-efficient, so
    // no need to have completely silent periods like we have with the TI CC2564.
    // When Android does a 100% duty cycle scan, it should pick up the low-duty cycle advertising
    // reasonably quickly.
    [1] = {
      .duration_secs = 5,
      .min_interval_slots = 244, // 152.5 ms -- Apple-recommended advertising interval
      .max_interval_slots = 256, // 160.0 ms
    },
    [2] = {
      .duration_secs = 20,
      .min_interval_slots = 1636, // 1022.5 ms -- Apple-recommended advertising interval
      .max_interval_slots = 1656, // 1035.0 ms
    },
    [3] = {
      .duration_secs = GAPLE_ADVERTISING_DURATION_LOOP_AROUND,
      .loop_around_index = 1,
    },
  };
  *num_terms_out = ARRAY_LENGTH(s_advert_terms);
  return s_advert_terms;
}
