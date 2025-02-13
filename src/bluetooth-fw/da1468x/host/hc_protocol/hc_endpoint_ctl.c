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

#include "kernel/util/sleep.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include <inttypes.h>

static bool prv_send_ctl_cmd(HcMessageID_Ctl cmd_id,
                             const uint8_t *payload, size_t payload_len) {
  HcProtocolMessage *response =
      hc_protocol_enqueue_with_payload_and_expect(HcEndpointID_Ctl, cmd_id, payload, payload_len);
  if (!response) {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to receive response to ctl command %"PRIu8, cmd_id);
    return false;
  }

  PBL_LOG(LOG_LEVEL_INFO, "Received ctl response for command %"PRIu8, cmd_id);
  kernel_free(response);
  return true;
}

bool hc_endpoint_ctl_init_sync(const BTDriverConfig *config) {
  return prv_send_ctl_cmd(HcMessageID_Ctl_Init, (const uint8_t *)config, sizeof(*config));
}

bool hc_endpoint_ctl_shutdown_sync(void) {
  const bool rv = prv_send_ctl_cmd(HcMessageID_Ctl_Shutdown, NULL, 0);
  PBL_LOG(LOG_LEVEL_INFO, "Got confirmation, BT Chip should be going into a deep sleep...");
  return rv;
}
