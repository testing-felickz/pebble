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

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gatt_discovery.h>
#include <bluetooth/gatt_service_types.h>
#include <bluetooth/hci_types.h>

#include <util/attributes.h>

typedef enum {
  HcMessageID_Discovery_Start = 0x01, // Host -> BT Controller
  HcMessageID_Discovery_Stop = 0x02, // Host -> BT Controller
  HcMessageID_Discovery_Service_Found = 0x03, // BT Controller -> Host
  HcMessageID_Discovery_Complete = 0x04, // BT Controller -> Host
  HcMessageID_Discovery_Service_Changed_Handle = 0x05 // BT Controller -> Host
} HcMessageID_Discovery;

typedef struct PACKED HcProtocolDiscoveryStartPayload {
  BTDeviceInternal address;
  ATTHandleRange range;
} HcProtocolDiscoveryStartPayload;

typedef struct PACKED HcProtocolDiscoveryServiceFoundPayload {
  BTDeviceInternal address;
  GATTService service;
} HcProtocolDiscoveryServiceFoundPayload;

typedef struct PACKED HcProtocolDiscoveryCompletePayload {
  BTDeviceInternal address;
  HciStatusCode status;
} HcProtocolDiscoveryCompletePayload;

typedef struct PACKED HcProtocolDiscoveryServiceChangedHandlePayload {
  BTDeviceInternal address;
  uint16_t handle;
} HcProtocolDiscoveryServiceChangedHandlePayload;

void hc_endpoint_discovery_handler(const HcProtocolMessage *msg);

//! Host -> BT Controller
bool hc_endpoint_discovery_start(const ATTHandleRange *range, const BTDeviceInternal *address);
bool hc_endpoint_discovery_stop(const BTDeviceInternal *address);

//! BT Controller -> Host
void hc_endpoint_discovery_send_service_found(
    const HcProtocolDiscoveryServiceFoundPayload *payload, uint32_t payload_size);
void hc_endpoint_discovery_complete(const BTDeviceInternal *address, HciStatusCode status);
void hc_endpoint_discovery_service_changed_handle(const BTDeviceInternal *address, uint16_t handle);
