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
#include "ble_common.h"
#include "ble_mgr.h"
#include "ble_mgr_irb_common.h"
#include "co_bt.h"
#include "hc_protocol/hc_endpoint_hci.h"
#include "hci_rom_passthrough.h"
#include "system/hexdump.h"
#include "system/logging.h"

#include "llc.h"
#include "llm.h"
#include "gapc.h"

#include <util/attributes.h>

#include <string.h>
#include <stdio.h>

bool hci_rom_passthrough_send_cmd(
    uint16_t ogf, uint16_t ocf, const uint8_t *param_buf, uint8_t param_length) {

  uint8_t msg_size = sizeof( irb_ble_stack_msg_t ) + sizeof(hci_cmd_msg_t) + param_length;
  irb_ble_stack_msg_t *msg_buf = OS_MALLOC(msg_size); // memory free'd in ad_ble.c:ble_task()

  *msg_buf = (irb_ble_stack_msg_t) {
    .op_code = IRB_BLE_STACK_MSG,
    .msg_type = HCI_CMD_MSG,
    .msg_size = HCI_CMD_HEADER_LENGTH + param_length,
  };

  hci_cmd_msg_t *hci_cmd = (hci_cmd_msg_t *)&msg_buf->msg;
  *hci_cmd = (hci_cmd_msg_t) {
    .op_code = HCI_OPCODE(ocf, ogf),
    .param_length = param_length,
  };

  if (param_length != 0) {
    memcpy(&hci_cmd->param, param_buf, param_length);
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Sending HCI CMD to ROM stack:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)hci_cmd, msg_buf->msg_size);

  bool result = (ad_ble_command_queue_send(&msg_buf, OS_QUEUE_FOREVER) == pdPASS);
  return result;
}

void hci_rom_passthrough_handle_evt(hci_evt_msg_t *hci_evt) {
  uint16_t payload_len = sizeof(hci_evt_msg_t) + hci_evt->param_length - sizeof(hci_evt->param);
  PBL_LOG(LOG_LEVEL_DEBUG, "HCI Event Response:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)hci_evt, payload_len);

  hc_endpoint_enqueue_hci_evt((uint8_t *)hci_evt, payload_len);
}

#if SUPPORTS_PACKET_LENGTH_EXTENSION
void hci_initiate_length_change(uint16_t conn_idx) {
  uint16_t connhdl = gapc_get_conhdl(conn_idx);

  struct llc_env_tag *llc_env_ptr = llc_env[connhdl];
  // The ROM HCI handler will only send a length request if it thinks we have requested a parameter
  // change. Change the setting here to force the negotiation to be sent.
  // Check out ROM function hci_le_set_data_length_cmd_handler()
  llc_env_ptr->connMaxTxOctets = LE_LENGTH_EXT_OCTETS_MIN + 1;

  // In llc_le_length_conn_init_func_wa() we set the RX window sizes to be the minimum packet size
  // so that in case the other side sends an LL_LENGTH_REQ and there are interopobility issues the
  // connection will start without changing the size. If this routine gets called, it's been
  // determined that the device connected to supports extended packets so bump our supported RX
  // window sizes to the max allowed so these values are sent as part of the LL_LENGTH_REQ
  llc_env_ptr->connMaxRxOctets = LE_LENGTH_EXT_OCTETS_MAX;
  llc_env_ptr->connMaxRxTime = LE_LENGTH_EXT_TIME_MAX;

  struct PACKED {
    uint16_t connhdl;
    uint16_t tx_octets;
    uint16_t tx_time;
  } params = {
    .connhdl = connhdl,
    .tx_octets = LE_LENGTH_EXT_OCTETS_MIN,
    .tx_time = LE_LENGTH_EXT_TIME_MIN,
  };

  uint16_t ocf = HCI_OP2OCF(HCI_LE_SET_DATA_LENGTH_CMD_OPCODE); // 0x22
  uint16_t ogf = HCI_OP2OGF(HCI_LE_SET_DATA_LENGTH_CMD_OPCODE); // 0x08

  hci_rom_passthrough_send_cmd(ogf, ocf, (uint8_t *)&params, sizeof(params));
}
#endif

void test_hci_passthrough(void) {
  static int i = 0;

  switch (i) {
    case 0:
      PBL_LOG(LOG_LEVEL_DEBUG, "===Reset===");
      hci_rom_passthrough_send_cmd(0x3, 0x3, NULL, 0);
      break;
    case 1: {
      PBL_LOG(LOG_LEVEL_DEBUG, "===LE Transmit Test===");
      uint8_t params[] = {0x00, 0x20, 0x01};
      hci_rom_passthrough_send_cmd(0x8, 0x1e, &params[0], sizeof(params));
      break;
    }
    case 2:
      PBL_LOG(LOG_LEVEL_DEBUG, "===LE Stop Tx Test===");
      hci_rom_passthrough_send_cmd(0x8, 0x1f, NULL, 0);
      break;
    case 3: {
      PBL_LOG(LOG_LEVEL_DEBUG, "===LE Receiver Test===");
      uint8_t params[] = { 0x00 };
      hci_rom_passthrough_send_cmd(0x8,  0x1d, &params[0], sizeof(params));
      break;
    }
    case 4:
      PBL_LOG(LOG_LEVEL_DEBUG, "===LE Stop Rx Test===");
      hci_rom_passthrough_send_cmd(0x8, 0x1f, NULL, 0);
      break;
    default:
      return;
  }
  i++;
}
