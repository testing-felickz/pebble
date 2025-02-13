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

#include "hc_protocol/hc_endpoint_chip_id.h"

#include "chip_id.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include <inttypes.h>

static void prv_handle_chip_info_request(const HcProtocolMessage *request) {
  const uint8_t *response_payload = NULL;
  DialogChipID chip_id;
  if (dialog_chip_id_copy(&chip_id)) {
    response_payload = (const uint8_t *)&chip_id;
  }
  // Send back empty response in case of failure.
  hc_protocol_enqueue_response(request, response_payload, response_payload ? sizeof(chip_id) : 0);
}

void hc_endpoint_chip_id_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Id_ChipInfo:
      prv_handle_chip_info_request(msg);
      break;

    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unknown cmd ID: 0x%"PRIx8, msg->command_id);
      break;
  }
}
