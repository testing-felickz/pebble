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
#include "util/attributes.h"

#include <inttypes.h>

typedef struct ble_evt_gattc_read_completed_t ble_evt_gattc_read_completed_t;
typedef struct Connection Connection;

typedef enum {
  HcMessageID_GapService_SetName = 0x1,
  HcMessageId_GapService_MtuChanged = 0x2,
  HcMessageID_GapService_DeviceNameRequest = 0x3,
  HcMessageID_GapService_DeviceNameRequest_All = 0x4,
  HcMessageID_GapService_SetLocalAddress = 0x5,
  HcMessageID_GapService_GeneratePrivateResolvable_address = 0x6,
} HcMessageID_GapService;

typedef struct PACKED HcProtocol_GapServiceMtuChanged {
  BTDeviceInternal addr;
  uint16_t mtu;
} HcProtocol_GapServiceMtuChanged;

typedef struct PACKED HcProtocol_GapDeviceNameResponseHeader {
  BTDeviceInternal addr;
  uint8_t name_length;
  uint8_t name[];
} HcProtocol_GapDeviceNameResponseHeader;

typedef struct PACKED HcProtocol_GapServiceSetLocalAddress {
  bool allow_cycling;
  BTDeviceAddress pinned_addr;
} HcProtocol_GapServiceSetLocalAddress;

typedef struct PACKED HcProtocol_GapServiceGeneratePrivateResolvableAddressResponse {
  BTDeviceAddress address;
} HcProtocol_GapServiceGeneratePrivateResolvableAddressResponse;

void hc_endpoint_gap_service_handler(const HcProtocolMessage *msg);
void hc_endpoint_gap_service_resp_handler(const HcProtocolMessage *msg);

void hc_endpoint_gap_service_mtu_changed(const Connection *connection, uint16_t mtu);
void hc_endpoint_gap_service_device_name_read(const ble_evt_gattc_read_completed_t *evt);

void hc_endpoint_gap_service_set_dev_name(const char *name);
void hc_endpoint_gap_service_device_name_request(const BTDeviceInternal *addr);
void hc_endpoint_gap_service_device_name_request_all(void);

void hc_endpoint_gap_service_set_local_address(bool allow_cycling,
                                               const BTDeviceAddress *pinned_address);

bool hc_endpoint_gap_service_generate_private_resolvable_address(BTDeviceAddress *address_out);
