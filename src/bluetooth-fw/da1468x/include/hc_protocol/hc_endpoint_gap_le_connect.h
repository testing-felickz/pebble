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
#include "util/attributes.h"

#include <bluetooth/gap_le_connect.h>

typedef enum {
  HcMessageID_GapLEConnect_ConnectionComplete = 0x1, // Controller -> Host
  HcMessageID_GapLEConnect_DisconnectionComplete = 0x2, // Controller -> Host
  HcMessageID_GapLEConnect_EncryptionChange = 0x3, // Controller -> Host
  HcMessageID_GapLEConnect_UpdateAddressAndIRK = 0x4, // Controller -> Host
  HcMessageID_GapLEConnect_Disconnect = 0x5, // Host -> Controller
  HcMessageID_GapLEConnect_PeerVersionInfo = 0x6, // Controller -> Host
} HcMessageID_GapLEConnect;

typedef struct PACKED HcGapLeConnectionData {
  BleConnectionCompleteEvent connection_complete_event;
  uint16_t mtu;
} HcGapLeConnectionData;

// On the Main MCU and handles messages sent by the Dialog chip
void hc_endpoint_gap_le_connect_handler(const HcProtocolMessage *msg);

//! Controller -> Host
void hc_endpoint_gap_le_connect_send_connection_complete(HcGapLeConnectionData *e);
void hc_endpoint_gap_le_connect_send_disconnection_complete(BleDisconnectionCompleteEvent *e);
void hc_endpoint_gap_le_connect_send_encryption_changed(BleEncryptionChange *e);
void hc_endpoint_gap_le_connect_send_address_and_irk_changed(BleAddressAndIRKChange *e);
void hc_endpoint_gap_le_connect_send_peer_version_info(BleRemoteVersionInfoReceivedEvent *e);

//! Host -> Controller
int hc_endpoint_gap_le_disconnect(const BTDeviceInternal *address);
