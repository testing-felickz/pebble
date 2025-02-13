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

#include <bluetooth/bt_test.h>
#include <bluetooth/hci_types.h>
#include <bluetooth/init.h>

#include "comm/ble/gap_le_connection.h"
#include "console/console_internal.h"
#include "console/prompt.h"
#include "console/serial_console.h"
#include "drivers/accessory.h"
#include "hc_protocol/hc_endpoint_chip_id.h"
#include "hc_protocol/hc_endpoint_hci.h"
#include "hc_protocol/hc_endpoint_test.h"
#include "kernel/pbl_malloc.h"
#include "services/common/system_task.h"
#include "system/logging.h"

#include <stdio.h>
#include <string.h>

static bool s_hci_passthrough_enabled = false;
static bool s_test_mode_enabled = false;
static BTDriverResponseCallback s_response_callback = NULL;

void bt_driver_test_start(void) {
  // we shouldn't need any special config for tests
  BTDriverConfig config = { };
  bt_driver_start(&config);

  s_test_mode_enabled = true;
}

void bt_driver_test_stop(void) {
  bt_driver_stop();
  s_test_mode_enabled = false;
}

void bt_driver_test_enter_hci_passthrough(void) {
  s_hci_passthrough_enabled = true;
}

bool bt_driver_test_enter_rf_test_mode(void) {
  return true;
}

void bt_driver_test_set_spoof_address(const BTDeviceAddress *addr) {
  prompt_send_response("NYI!");
}

#define HCI_CMD_HEADER_LEN (1 /* hci packet type */ + 2 /* op code */ + 1 /* param len */)
#define MAX_SUPPORTED_HCI_PARAM_LEN 25
#define MAX_SUPPORTED_HCI_CMD_LEN (MAX_SUPPORTED_HCI_PARAM_LEN + HCI_CMD_HEADER_LEN)

typedef struct {
  bool found_cmd_start;
  uint8_t param_len;
  uint8_t write_offset;
  uint8_t data_buf[MAX_SUPPORTED_HCI_CMD_LEN]; // holds one HCI cmd
} HciCmdInfo;

static void prv_handle_hci_cmd(void *hci_data) {
  uint8_t *data = hci_data;
  uint8_t payload_len = data[0];
  uint8_t *hci_cmd = &data[1];
  hc_endpoint_enqueue_hci_cmd(hci_cmd, payload_len);
  kernel_free(hci_data);
}

//! A barebones HCI CMD parser. Looks for an HCI start byte (0x1) and then
//! makes sure the param length is less than the static buffer we have for
//! holding a single command. If anything goes wrong, the state machine resets
//! and starts looking for 0x1 again. If we are not in the process of decoding
//! a command a ctrl-d will exit passthrough mode.
void bt_driver_test_handle_hci_passthrough_character(
    char c, bool *should_context_switch) {
  static HciCmdInfo s_hci_cmd_info = {};

  if (!s_hci_cmd_info.found_cmd_start) {
    if (c == 0x4) { // Exit bypass mode sequence
      serial_console_set_state(SERIAL_CONSOLE_STATE_LOGGING);
      s_hci_passthrough_enabled = false;
      return;
    }

    if (c != 0x1) { // HCI cmd should start with 0x1
      return;
    }

    s_hci_cmd_info.found_cmd_start = true;
  }

  if (s_hci_cmd_info.write_offset == 3) {
    s_hci_cmd_info.param_len = (uint8_t)c;

    if (s_hci_cmd_info.param_len > MAX_SUPPORTED_HCI_PARAM_LEN) {
      PBL_LOG(LOG_LEVEL_WARNING, "Longer HCI CMD than expected!");
      s_hci_cmd_info = (HciCmdInfo) { }; // reset
      return;
    }
  }

  // We've received an expected byte, so store it
  s_hci_cmd_info.data_buf[s_hci_cmd_info.write_offset] = c;
  s_hci_cmd_info.write_offset++;

  if (s_hci_cmd_info.write_offset == (HCI_CMD_HEADER_LEN + s_hci_cmd_info.param_len)) {
    // We have found an HCI command, forward to the dialog chip
    uint8_t payload_len = s_hci_cmd_info.write_offset;
    uint8_t buf_len = payload_len + 1;
    uint8_t *buf = kernel_zalloc_check(buf_len);
    buf[0] = payload_len;
    memcpy(&buf[1], &s_hci_cmd_info.data_buf[0], payload_len);

    bool should_context_switch;
    system_task_add_callback_from_isr(prv_handle_hci_cmd, buf, &should_context_switch);

    // reset our state machine
    s_hci_cmd_info = (HciCmdInfo) { };
  }
}

bool bt_driver_test_selftest(void) {
  DialogChipID chip_id;
  if (!hc_endpoint_chip_id_query_chip_info(&chip_id)) {
    return false;
  }
  // Sanity check the timestamp that should always be programmed:
  return (chip_id.info.timestamp > DIALOG_CHIP_ID_MIN_TIMESTAMP);
}

bool bt_driver_test_mfi_chip_selftest(void) {
  return false;
}

// A simple parser for HCI Command Complete events. If we are not in HCI
// passthrough mode, this is the only type of command we expect to receive

#define HCI_CMD_START    0x1
#define HCI_EVT_START    0x4

#define HCI_LE_RX_TEST_OCF   0x1D
#define HCI_LE_TX_TEST_OCF   0x1E
#define HCI_LE_TEST_STOP_OCF 0x1F

#define HCI_CMD_COMPLETE_CODE 0xe
#define OPCODE_TEST_CMDS_MSB 0x20 // LE Controller Commands OGF=0x8

static void prv_accessory_putstr(char *data) {
#if CAPABILITY_HAS_ACCESSORY_CONNECTOR
  accessory_send_data((const uint8_t *)data, strlen(data));
#endif
}

static void prv_dbgserial_putstr(char *data) {
  while (*data) {
    dbgserial_putchar(*data);
    data++;
  }
}

bool bt_test_hci_event_handled(const uint8_t *hci_msg, uint16_t payload_len) {
  // Don't intercept anything if we are in passthrough mode
  if (s_hci_passthrough_enabled) {
    return false;
  }

  // we expect to handle a "command complete" event so check for it
  if ((hci_msg[0] != HCI_EVT_START) || (hci_msg[1] != HCI_CMD_COMPLETE_CODE)) {
    return false;
  }

  typedef struct {
    uint8_t param_length;
    uint8_t num_cmd_pkts;
    uint8_t lsb_opcode;
    uint8_t msb_opcode;
    uint8_t payload[];
  } HciCmdCompleteEvt;

  HciCmdCompleteEvt *e = (HciCmdCompleteEvt *)&hci_msg[2];

  // simple sanity check on payload
  if ((e->param_length == 0) || (e->msb_opcode != OPCODE_TEST_CMDS_MSB)) {
    return false;
  }

  void (*flush_data)(char *) = serial_console_is_prompt_enabled() ?
      prv_dbgserial_putstr : prv_accessory_putstr;

  char buf[40];
  uint8_t status = e->payload[0];
  snprintf(buf, sizeof(buf), "%s: cmd = 0x%x Status = 0x%x\r\n",
           (status == HciStatusCode_Success) ? "Success!" : "Failure",
           (int)e->lsb_opcode, (int)status);
  flush_data(buf);

  snprintf(buf, sizeof(buf), "HCI Event:");
  flush_data(buf);

  for (int i = 0; i < payload_len; i++) {
    snprintf(buf, sizeof(buf), " 0x%02x", hci_msg[i]);
    flush_data(buf);
  }

  flush_data("\r\n");

  if (s_response_callback) {
    s_response_callback(status, e->payload);
  }

  return true;
}

bool bt_test_chip_in_test_mode(void) {
  return s_test_mode_enabled;
}

static bool prv_in_test_mode(void) {
  if (!s_test_mode_enabled) {
    char buf[80];
    prompt_send_response_fmt(
        buf, sizeof(buf), "Not in test mode, run 'bt test start' first!");
    return false;
  }
  return true;
}

void bt_driver_le_transmitter_test(
    uint8_t tx_channel, uint8_t tx_packet_length, uint8_t packet_payload_type) {
  if (!prv_in_test_mode()) {
    return;
  }

  uint8_t tx_test_cmd[] = {
    HCI_CMD_START,
    HCI_LE_TX_TEST_OCF,
    OPCODE_TEST_CMDS_MSB,
    0x3, // param length
    tx_channel,
    tx_packet_length,
    packet_payload_type
  };

  hc_endpoint_enqueue_hci_cmd(tx_test_cmd, sizeof(tx_test_cmd));
}

void bt_driver_le_receiver_test(uint8_t rx_channel) {
  if (!prv_in_test_mode()) {
    return;
  }

  uint8_t rx_test_cmd[] = {
    HCI_CMD_START,
    HCI_LE_RX_TEST_OCF,
    OPCODE_TEST_CMDS_MSB,
    0x1, // param length
    rx_channel
  };
  hc_endpoint_enqueue_hci_cmd(rx_test_cmd, sizeof(rx_test_cmd));
}

void bt_driver_le_test_end(void) {
  if (!prv_in_test_mode()) {
    return;
  }

  uint8_t test_end_cmd[] = {
    HCI_CMD_START,
    HCI_LE_TEST_STOP_OCF,
    OPCODE_TEST_CMDS_MSB,
    0x0 // param length
  };
  hc_endpoint_enqueue_hci_cmd(test_end_cmd, sizeof(test_end_cmd));
}

void bt_driver_register_response_callback(BTDriverResponseCallback callback) {
  s_response_callback = callback;
}

void bt_driver_start_unmodulated_tx(uint8_t tx_channel) {
  if (!prv_in_test_mode()) {
    return;
  }

  hc_endpoint_test_unmodulated_tx_test_start(tx_channel);
}

void bt_driver_stop_unmodulated_tx(void) {
  if (!prv_in_test_mode()) {
    return;
  }

  hc_endpoint_test_unmodulated_tx_test_stop();
}
