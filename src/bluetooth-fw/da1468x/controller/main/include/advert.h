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

#include "advert_state.h"

// Dialog SDK:
#include "ble_common.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ble_evt_gap_adv_completed ble_evt_gap_adv_completed_t;
typedef struct BLEAdData BLEAdData;

ble_error_t advert_set_interval(uint16_t min_slots, uint16_t max_slots);

//! @return The current state, returned for debugging/logging purposes.
//! It's possible this is not AdvertState_Running, in case advertising was paused or is still
//! stopping.
AdvertState advert_enable(void);

void advert_set_data(const BLEAdData *ad_data);

void advert_disable(void);

void advert_handle_completed(const ble_evt_gap_adv_completed_t *evt);

void advert_init(void);
