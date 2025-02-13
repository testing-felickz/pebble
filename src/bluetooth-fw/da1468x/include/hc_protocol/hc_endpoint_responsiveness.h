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

#include "bluetooth/responsiveness.h"
#include "hc_protocol/hc_protocol.h"

#include "util/attributes.h"

typedef enum {
  HcMessageID_Id_ConnParamUpdateReq = 0x01,
  HcMessageID_Id_ConnParamUpdateResponse = 0x02,
} HcMessageID_Responsiveness;

typedef struct PACKED {
  BTDeviceInternal address;
  BleConnectionParamsUpdateReq params;
} HcProtocolMessageResponsivenessPayload;

void hc_endpoint_responsiveness_handler(const HcProtocolMessage *msg);

//! Host -> BT Controller
bool hc_endpoint_responsiveness_request_update(
    const BTDeviceInternal *addr, const BleConnectionParamsUpdateReq *params);

//! BT Controller -> Host
void hc_endpoint_responsiveness_notify_update(
    const BleConnectionParams *params, const BTDeviceInternal *addr, HciStatusCode status);
