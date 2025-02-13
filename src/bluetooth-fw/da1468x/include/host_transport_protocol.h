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

#include "util/attributes.h"

#include <stdint.h>

// Design doc:
// https://docs.google.com/document/d/1or2Ygs3sWt_5XNW_Mpe3Vxmhwuh3DTzdgZr6QlEe7iQ/edit#

// TODO: Arbitrary for values. At some point we should evaluate if these sizes
// are too big or small, see PBL-36239
#define HOST_TRANSPORT_HOST_RX_BUFFER_SIZE (2048)
#define HOST_TRANSPORT_HOST_TX_BUFFER_SIZE (2048)
#define HOST_TRANSPORT_CTLR_RX_BUFFER_SIZE (1024)
#define HOST_TRANSPORT_CTLR_TX_BUFFER_SIZE (1024)

typedef enum {
  SPITransportMsgID_Status = 0x88,
} SPITransportMsgID;

typedef struct PACKED SPITransportMsgStatus {
  SPITransportMsgID msg_id:8;
  uint16_t bytes_sendable_count;
  uint16_t bytes_receivable_count;
  uint32_t crc;
} SPITransportMsgStatus;

typedef struct SPITransportMsgFooter {
  uint32_t crc;
} SPITransportMsgFooter;
