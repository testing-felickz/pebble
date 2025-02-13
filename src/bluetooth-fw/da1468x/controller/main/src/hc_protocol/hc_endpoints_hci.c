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

#include "ad_ble.h"
#include "co_bt.h"
#include "hc_protocol/hc_endpoint_hci.h"
#include "hc_protocol/hc_protocol.h"
#include "hci_rom_passthrough.h"
#include "system/hexdump.h"
#include "system/logging.h"

#include <stdio.h>
#include <string.h>

void hc_endpoint_hci_handler(const HcProtocolMessage *msg) {
  if (msg->command_id != HcMessageID_Hci_Cmd) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unhandled HCI command id: 0x%x", (int)msg->command_id);
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "HCI CMD Received:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)msg, msg->message_length);

  hci_cmd_msg_t *hci_cmd = (hci_cmd_msg_t *)&msg->payload[0];

  uint16_t opcode = hci_cmd->op_code;
  hci_rom_passthrough_send_cmd(
      HCI_OP2OGF(opcode), HCI_OP2OCF(opcode), &hci_cmd->param[0],
      hci_cmd->param_length);
}

void hc_endpoint_enqueue_hci_evt(const uint8_t *hci_evt, uint8_t payload_len) {
  uint8_t hc_message_len = sizeof(HcProtocolMessage) + 1 /* for HCI_EVT_MSG */ + payload_len;
  uint8_t hc_protocol_message[hc_message_len];

  HcProtocolMessage *hc_msg = (HcProtocolMessage *)&hc_protocol_message[0];
  memset(hc_msg, 0x00, hc_message_len);

  *hc_msg = (HcProtocolMessage) {
    .message_length = sizeof(hc_protocol_message),
    .endpoint_id = HcEndpointID_Hci,
    .command_id = HcMessageID_Hci_Evt,
  };

  uint8_t *hc_msg_data = &hc_msg->payload[0];
  *hc_msg_data++ = 0x4;

  memcpy(hc_msg_data, hci_evt, payload_len);

  PBL_LOG(LOG_LEVEL_DEBUG, "Sending:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)hc_msg, hc_message_len);

  hc_protocol_enqueue(hc_msg);
}
