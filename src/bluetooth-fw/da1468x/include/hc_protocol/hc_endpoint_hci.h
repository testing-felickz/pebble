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

typedef enum {
  HcMessageID_Hci_Cmd = 0x01,
  HcMessageID_Hci_Evt = 0x04,
} HcMessageID_Hci;

void hc_endpoint_hci_handler(const HcProtocolMessage *msg);
//! Controller -> Main MCU (Host)
void hc_endpoint_enqueue_hci_evt(const uint8_t *hci_evt_buf, uint8_t payload_len);
//! Main MCU (Host) -> Controller
void hc_endpoint_enqueue_hci_cmd(const uint8_t *hci_buf, uint8_t payload_len);
