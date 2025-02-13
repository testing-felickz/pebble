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
#include "hc_protocol/hc_protocol.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"

void bt_driver_le_test_pa(BtlePaConfig option) {
  hc_protocol_enqueue_with_payload(HcEndpointID_Test, HcMessageID_Test_Config_PA,
                                   &option, sizeof(option));
}

void hc_endpoint_test_unmodulated_tx_test_start(uint8_t tx_channel) {
  HcTestUnmodTxStart tx_test_payload = {
    .tx_channel = tx_channel
  };
  hc_protocol_enqueue_with_payload(
      HcEndpointID_Test, HcMessageID_Test_UnmodulatedTxStart, (uint8_t *)&tx_test_payload,
      sizeof(tx_test_payload));
}

void hc_endpoint_test_unmodulated_tx_test_stop(void) {
  hc_protocol_enqueue_with_payload(HcEndpointID_Test, HcMessageID_Test_UnmodulatedTxStop, NULL, 0);
}

void bt_driver_core_dump(BtleCoreDump type) {
  uint8_t option = (uint8_t)type;
  hc_protocol_enqueue_with_payload(HcEndpointID_Test, HcMessageID_Test_Core_Dump,
                                   &option, sizeof(option));
}

void bt_driver_send_sleep_test_cmd(bool force_ble_sleep) {
  HcTestSleep args = {
    .force_sleep = force_ble_sleep
  };

  HcProtocolMessage *resp = hc_protocol_enqueue_with_payload_and_expect(
      HcEndpointID_Test, HCMessageID_Test_Sleep, (uint8_t *)&args, sizeof(args));

  // Nothing useful encoded in the response, just forcing some extra SPI traffic for the purposes
  // of this test
  kernel_free(resp);
}
