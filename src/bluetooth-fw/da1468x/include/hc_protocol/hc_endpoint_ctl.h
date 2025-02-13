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

#include <bluetooth/init.h>

typedef enum {
  HcMessageID_Ctl_Init = 0x01,
  HcMessageID_Ctl_Shutdown = 0x02,
} HcMessageID_Ctl;

void hc_endpoint_ctl_handler(const HcProtocolMessage *msg);

//! Sends the init command and blocks until ack has been received or timeout is hit.
bool hc_endpoint_ctl_init_sync(const BTDriverConfig *config);

//! Sends the shutdown command and blocks until ack has been received or timeout is hit.
bool hc_endpoint_ctl_shutdown_sync(void);
