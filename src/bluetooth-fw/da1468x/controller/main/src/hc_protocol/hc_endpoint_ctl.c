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

#include "hc_protocol/hc_endpoint_ctl.h"

#include "ble_task.h"
#include "power.h"
#include "reboot_reason.h"
#include "system/logging.h"
#include "tasks.h"

#include <bluetooth/init.h>

#include <inttypes.h>

static void prv_handle_init(const HcProtocolMessage *request) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Got init message!");

  const BTDriverConfig *config = (const BTDriverConfig *)request->payload;
  ble_task_init(config);

  hc_protocol_enqueue_response(request, NULL, 0);
}

static void prv_handle_shutdown(const HcProtocolMessage *request) {
#if 0 // Fixme once some space is really freed up
  // Dump some statistics on stack usage before we shutdown
  tasks_dump_free_space();
#endif

  PBL_LOG(LOG_LEVEL_DEBUG, "Got shutdown message! Going to fall into a deep sleep...");

  // Set the reboot reason to signify we shut down gracefully
  RebootReason reason = {
    .code = RebootReasonCode_Shutdown,
  };
  // FIXME PBL-38181: For some reason, the reboot reason shutdown never gets persisted/recovered
  reboot_reason_set(&reason);

  // Send empty response as acknowledgement:
  hc_protocol_enqueue_response(request, NULL, 0);

  power_enter_hibernation();
}

void hc_endpoint_ctl_handler(const HcProtocolMessage *request) {
  switch (request->command_id) {
    case HcMessageID_Ctl_Init:
      prv_handle_init(request);
      break;

    case HcMessageID_Ctl_Shutdown:
      prv_handle_shutdown(request);
      break;

    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unknown command 0x%"PRIx8, request->command_id);
      break;
  }
}
