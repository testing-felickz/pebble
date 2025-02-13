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

#include "dialog_chip_id.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include <inttypes.h>

void hc_endpoint_chip_id_handler(const HcProtocolMessage *msg) {
  PBL_LOG(LOG_LEVEL_ERROR, "Unknown cmd ID: 0x%"PRIx8, msg->command_id);
}

bool hc_endpoint_chip_id_query_chip_info(DialogChipID *chip_id_out) {
  HcProtocolMessage request = {
    .message_length = sizeof(request),
    .endpoint_id = HcEndpointID_Id,
    .command_id = HcMessageID_Id_ChipInfo,
  };
  HcProtocolMessage *response = hc_protocol_enqueue_and_expect(&request);
  if (!response) {
    return false;
  }
  if (response->message_length < sizeof(HcProtocolMessage) + sizeof(DialogChipID)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Received error response.");
    return false;
  }
  memcpy(chip_id_out, response->payload, sizeof(*chip_id_out));
  kernel_free(response);
  return true;
}
