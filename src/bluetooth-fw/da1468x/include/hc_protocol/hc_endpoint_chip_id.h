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

#include "dialog_chip_id.h"
#include "hc_protocol/hc_protocol.h"

#include <stddef.h>

typedef enum {
  HcMessageID_Id_ChipInfo = 0x01,
} HcMessageID_Id;

void hc_endpoint_chip_id_handler(const HcProtocolMessage *msg);

typedef void (*HcEndpointIdResponseHandler)(const DialogChipID *chip_id);

void hc_endpoint_chip_id_send_request(HcEndpointIdResponseHandler *response_handler);

//! @return False if the request failed.
bool hc_endpoint_chip_id_query_chip_info(DialogChipID *chip_id_out);
