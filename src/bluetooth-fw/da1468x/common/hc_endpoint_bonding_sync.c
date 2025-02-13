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

#include "bonding_sync_impl.h"
#include "hc_protocol/hc_endpoint_bonding_sync.h"
#include "hc_protocol/hc_protocol.h"

#include "kernel/pbl_malloc.h"
#include "system/logging.h"

#include <bluetooth/bonding_sync.h>

#include <stdint.h>

void hc_endpoint_bonding_sync_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_BondingSync_AddBonding:
      bonding_sync_handle_hc_add((const BleBonding *)msg->payload);
      break;
    case HcMessageID_BondingSync_RemoveBonding:
      bonding_sync_handle_hc_remove((const BleBonding *)msg->payload);
      break;

    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unknown command ID: %"PRIu8, msg->command_id);
      break;
  }
}

static void prv_send_cmd(HcMessageID_BondingSync cmd_id, const BleBonding *bonding) {
  hc_protocol_enqueue_with_payload(HcEndpointID_BondingSync, cmd_id,
                                                  (const uint8_t *)bonding, sizeof(*bonding));
}

void hc_endpoint_bonding_sync_add(const BleBonding *bonding) {
  prv_send_cmd(HcMessageID_BondingSync_AddBonding, bonding);
}

void hc_endpoint_bonding_sync_remove(const BleBonding *bonding) {
  prv_send_cmd(HcMessageID_BondingSync_RemoveBonding, bonding);
}
