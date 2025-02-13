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

#include "kernel/pbl_malloc.h"
#include "system/logging.h"
#include "logging/binary_logging.h"

static void prv_log_string_v1(BinLogMessage_String_v1 *msg);
#if PBL_LOGS_HASHED
static void prv_log_param_v1(BinLogMessage_Param_v1 *msg);
#else
static void prv_log_unhashed_v1(BinLogMessage_Unhashed_v1 *msg);
#endif

void hc_endpoint_logging_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Logging_LogMsg: {
        BinLogMessage_Header *header = (BinLogMessage_Header *)msg->payload;
        switch (header->version) {
          case BINLOGMSG_VERSION_STRING_V1:
            prv_log_string_v1((BinLogMessage_String_v1 *)header);
            break;
          case BINLOGMSG_VERSION_PARAM_V1:
#if PBL_LOGS_HASHED
            prv_log_param_v1((BinLogMessage_Param_v1 *)header);
#else
            PBL_LOG(LOG_LEVEL_ERROR, "HcLog: received hashed log in unhashed build");
#endif
            break;
          case BINLOGMSG_VERSION_UNHASHED_V1:
#if PBL_LOGS_HASHED
            PBL_LOG(LOG_LEVEL_ERROR, "HcLog: received unhashed log in hashed build");
#else
            prv_log_unhashed_v1((BinLogMessage_Unhashed_v1 *)header);
#endif
            break;
          default:
            PBL_LOG(LOG_LEVEL_ERROR, "HcLog: unexpected log header version %d", header->version);
            break;
        }
      }
      break;
    default:
      PBL_LOG(LOG_LEVEL_INFO, "HcLog: unhandled message id: %d\n", msg->command_id);
      break;
  }
}

void hc_endpoint_logging_set_level(uint8_t level) {
  hc_protocol_enqueue_with_payload(HcEndpointID_Logging, HcMessageID_Logging_SetLevel,
                                   &level, sizeof(level));
}

bool hc_endpoint_logging_get_level(uint8_t *level) {
  HcProtocolMessage request = {
    .message_length = sizeof(request),
    .endpoint_id = HcEndpointID_Logging,
    .command_id = HcMessageID_Logging_GetLevel,
  };
  HcProtocolMessage *response = hc_protocol_enqueue_and_expect(&request);
  if (!response) {
    return false;
  }
  if (response->message_length < sizeof(HcProtocolMessage) + sizeof(uint8_t)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Received error response.");
    return false;
  }
  *level = response->payload[0];
  kernel_free(response);
  return true;
}

static void prv_log_string_v1(BinLogMessage_String_v1 *msg) {
  PBL_LOG(LOG_LEVEL_ALWAYS, "%s", msg->string);
}

#if PBL_LOGS_HASHED

static void prv_log_param_v1(BinLogMessage_Param_v1 *msg) {
  // HACK ALERT: we want to call a var-args function but we don't want to create our own stack
  // frame. That would be ugly. Since we can include more arguments than the callee uses, we'll just
  // include the maximum number of parameters (limited to 7 by New Logging) and ignore the
  // overhead. TODO: once the OS switches to binary logging, this will simply be a packet copy with
  // no further watch-side processing.
  uint32_t args[7] = { 0 };

  // Unpack the arguments
  uint32_t *arg = msg->body.payload;
  unsigned int arg_index = 0;

  while ((uint8_t *)arg < (uint8_t *)msg + msg->header.length) {
    if (((msg->body.msgid.str_index_1 != 0) && (arg_index + 1 == (msg->body.msgid.str_index_1))) ||
        ((msg->body.msgid.str_index_2 != 0) && (arg_index + 1 == (msg->body.msgid.str_index_2)))) {
      // String. TODO: the string is null terminated for now.
      BinLogMessage_StringParam *str_param = (BinLogMessage_StringParam *)arg;
      // Send the string pointer!
      args[arg_index] = (uint32_t)str_param->string;
      arg += (str_param->length + 1 + (sizeof(uint32_t) - 1)) / sizeof(uint32_t);
    } else {
      // Integer
      args[arg_index] = *arg;
      arg++;
    }
    arg_index++;
  }

  // Fixup the hash (add the #args, ignore the task id for now, leave the level 0) TODO.
  uint32_t hash = ((msg->body.msgid.msg_id & MSGID_STR_AND_HASH_MASK) |
                   ((arg_index & PACKED_NUM_FMT_MASK) << PACKED_NUM_FMT_OFFSET));
  // The core must be in the correct position for pbl_log_hashed_core.
  uint32_t core = (msg->body.msgid.msg_id & (PACKED_CORE_MASK << PACKED_CORE_OFFSET));

  pbl_log_hashed_core(core, hash, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
}

#else // PBL_LOGS_HASHED

static void prv_log_unhashed_v1(BinLogMessage_Unhashed_v1 *msg) {
  // Force NULL termination on the filename
  char filename[17] = { 0 };
  memcpy(filename, msg->body.filename, 16);

  pbl_log(msg->body.level, filename, msg->body.line_number, (const char *)msg->body.string);
}

#endif // PBL_LOGS_HASHED
