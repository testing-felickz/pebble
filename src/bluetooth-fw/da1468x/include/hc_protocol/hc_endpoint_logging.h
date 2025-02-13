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
  HcMessageID_Logging_LogMsg = 0x1,
  HcMessageID_Logging_SetLevel = 0x2,
  HcMessageID_Logging_GetLevel = 0x3,
} HcMessageID_LoggingService;

void hc_endpoint_logging_handler(const HcProtocolMessage *msg);

//! Host -> Controller
void hc_endpoint_logging_set_level(uint8_t level);
bool hc_endpoint_logging_get_level(uint8_t *level);

//! Controller -> Host
// The caller must have crafted the HcProtocolMessage + payload correctly
// Returns: true, if sent or if host_transport isn't yet available. False, otherwise.
bool hc_endpoint_logging_send_msg(HcProtocolMessage *msg);
