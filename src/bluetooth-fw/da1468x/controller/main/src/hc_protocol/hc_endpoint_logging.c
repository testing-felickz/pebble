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

#include "hc_protocol/hc_endpoint_logging.h"
#include "system/logging.h"

extern bool host_transport_ready;

void hc_endpoint_logging_handler(const HcProtocolMessage *msg) {
  uint8_t level;

  switch (msg->command_id) {
    case HcMessageID_Logging_SetLevel:
      level = msg->payload[0];
      pbl_log_set_level(level);
      break;
    case HcMessageID_Logging_GetLevel:
      level = pbl_log_get_level();
      hc_protocol_enqueue_response(msg, &level, sizeof(level));
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcLogging: unhandled message id: %d", msg->command_id);
      break;
  }
}

// The caller must have created the HcProtocolMessage + payload correctly.
bool hc_endpoint_logging_send_msg(HcProtocolMessage *msg) {
  if (!host_transport_ready) {
    return true; // This is not an "out of space in the ring buffer" situation. Can't alert user.
  }

  msg->endpoint_id = HcEndpointID_Logging;
  msg->command_id = HcMessageID_Logging_LogMsg;
  return hc_protocol_enqueue(msg);
}
