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

#include "hc_protocol/hc_endpoint_advert.h"
#include "hc_protocol/hc_protocol.h"

#include "advert_state.h"

#include <inttypes.h>

#include "system/logging.h"


void hc_endpoint_advert_resp_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Advert_Enable:
      PBL_LOG(LOG_LEVEL_DEBUG, "Advert Enable: %"PRIu8, msg->payload[0]);
      break;
    case HcMessageID_Advert_SetAdvData:
      PBL_LOG(LOG_LEVEL_DEBUG, "Advert SetAdvData: %"PRIu8, msg->payload[0]);
      break;
    case HcMessageID_Advert_Disable:
      // NYI
      break;
    default:
      PBL_LOG(LOG_LEVEL_DEBUG, "HcAdvert Response: unhandled message id: %d", msg->command_id);
  }
}
