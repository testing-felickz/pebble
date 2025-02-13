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

#pragma once

#include "hc_protocol/hc_protocol.h"

#include <bluetooth/bt_test.h>
#include <util/attributes.h>

#include <inttypes.h>

typedef enum {
  HcMessageID_Test_Config_PA = 0x01,
  HcMessageID_Test_UnmodulatedTxStart = 0x02,
  HcMessageID_Test_UnmodulatedTxStop = 0x03,
  HcMessageID_Test_Core_Dump = 0x04,
  HCMessageID_Test_Sleep = 0x5,
} HcMessageID_Test;

typedef struct PACKED HcTestSleep {
  bool force_sleep;
} HcTestSleep;

typedef struct PACKED HcTestUnmodTxStart {
  uint8_t tx_channel;
} HcTestUnmodTxStart;

// Handler on BT Controller side
void hc_endpoint_test_handler(const HcProtocolMessage *msg);

// Host -> Controller
void hc_endpoint_test_unmodulated_tx_test_start(uint8_t tx_channel);
void hc_endpoint_test_unmodulated_tx_test_stop(void);
