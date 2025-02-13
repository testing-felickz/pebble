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
#include "hc_protocol/hc_protocol.h"

// Dialog SDK:
#include "ble_common.h"

#include <util/attributes.h>

typedef enum {
  HcMessageID_Advert_Enable = 0x01,
  HcMessageID_Advert_Disable = 0x02,
  HcMessageID_Advert_SetAdvData = 0x03,
} HcMessageID_Advert;


typedef struct PACKED HcAdvertEnableData {
  uint16_t min_interval_ms;
  uint16_t max_interval_ms;
} HcAdvertEnableData;

typedef struct PACKED HcAdvertEnableResponseData {
  ble_error_t error;
  AdvertState current_state;
} HcAdvertEnableResponseData;

void hc_endpoint_advert_handler(const HcProtocolMessage *msg);

void hc_endpoint_advert_resp_handler(const HcProtocolMessage *msg);
