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

#include <bluetooth/bonding_sync.h>

typedef enum {
  HcMessageID_BondingSync_AddBonding = 0x01,
  HcMessageID_BondingSync_RemoveBonding = 0x02,
} HcMessageID_BondingSync;

void hc_endpoint_bonding_sync_handler(const HcProtocolMessage *msg);

//! Host => Controller and Controller => Host
void hc_endpoint_bonding_sync_add(const BleBonding *bonding);

//! Host => Controller
void hc_endpoint_bonding_sync_remove(const BleBonding *bonding);
