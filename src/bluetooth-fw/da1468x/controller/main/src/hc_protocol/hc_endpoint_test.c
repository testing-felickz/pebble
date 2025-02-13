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

#include "hc_protocol/hc_endpoint_test.h"
#include "system/logging.h"
#include <bluetooth/bt_test.h>

#include "core_dump.h"

// Dialog APIs
#include "sdk_defs.h"
#include "hw_rf.h"

void hw_fem_set_pin_config(BtlePaConfig config);
void rwip_prevent_sleep(bool enable);
void ble_force_wakeup();


static void prv_hard_fault(void) {
  // Store 0x00000000 at 0x000003. *(int *)0x03 = 0
  __asm("mov r0, #0\n\t"
        "mov r1, #3\n\t"
        "str r0, [r1]\n\t");
  PBL_LOG(LOG_LEVEL_ERROR, "HcTest: CoreDump - Unaligned Access succeeded!?!");
}

static void prv_handle_core_dump_request(BtleCoreDump type) {
  switch (type) {
    case BtleCoreDump_UserRequest:
      core_dump(true);
      break;
    case BtleCoreDump_ForceHardFault:
      prv_hard_fault();
      break;
    case BtleCoreDump_Watchdog:
      while (1) {} // wedge the task
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcTest: unhandled core dump id: %d", type);
      break;
    }
}

static void prv_enable_continuous_wave_mode(const HcTestUnmodTxStart *cmd) {
  PBL_LOG(LOG_LEVEL_ALWAYS, "TX'ing unmodulated CW on BT channel %d", cmd->tx_channel);

  // Prevent the RW ROM from powering down the radio in the future
  rwip_prevent_sleep(true);

  // Power up the radio if the RW ROM already put us to sleep
  ble_force_wakeup();

  // Wait for the BLE Radio to be up
  while (!REG_GETF(CRG_TOP, SYS_STAT_REG, BLE_IS_UP)) {}

  // Start the CW pattern
  hw_rf_start_continuous_wave(0x1, cmd->tx_channel);
}

extern void cm_lp_clk_force_available(bool force_available);

static void prv_handle_sleep_test_cmd(const HcProtocolMessage *msg) {
  const HcTestSleep *sleep_test = (HcTestSleep *)&msg->payload[0];

  // Dialog gives the 32K clock time to settle by default. The settling time is 8s. Since we use a
  // digital clock this isn't really necessary. However, since there are issues around
  // entering/exiting sleep we may want a delay of some sort before sleeping. Thus, so the sleep
  // test can fail faster let's just override this check rather than set the timeout to 0
  cm_lp_clk_force_available(sleep_test->force_sleep);


  uint8_t response[20] = { 0 };
  hc_protocol_enqueue_response(msg, &response[0], sizeof(response));
}

void hc_endpoint_test_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
#if PLATFORM_ROBERT
    case HcMessageID_Test_Config_PA:
      PBL_LOG(LOG_LEVEL_INFO, "HcTest: Config PA: %d", msg->payload[0]);
      hw_fem_set_pin_config(msg->payload[0]);
      break;
#endif
    case HCMessageID_Test_Sleep:
      prv_handle_sleep_test_cmd(msg);
      break;
    case HcMessageID_Test_UnmodulatedTxStart:
      prv_enable_continuous_wave_mode((HcTestUnmodTxStart *)&msg->payload[0]);
      break;
    case HcMessageID_Test_UnmodulatedTxStop:
      rwip_prevent_sleep(false);
      hw_rf_stop_continuous_wave();
      break;
    case HcMessageID_Test_Core_Dump:
      prv_handle_core_dump_request((BtleCoreDump) msg->payload[0]);
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcTest: unhandled message id: %d", msg->command_id);
      break;
  }
}
