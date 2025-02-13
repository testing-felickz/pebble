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
#include <bluetooth/gap_le_connect.h>
#include <util/attributes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  HcMessageID_Gatt_Read = 0x01,
  HcMessageID_Gatt_ReadCompleted = 0x02,
  HcMessageID_Gatt_Write = 0x03,
  HcMessageID_Gatt_WriteNoResponse = 0x04,
  HcMessageID_Gatt_WriteCompleted = 0x05,
  HcMessageID_Gatt_Notification = 0x06,
  HcMessageID_Gatt_Indication = 0x07,
} HcMessageID_Gatt;

typedef struct PACKED HcGattHdr {
  BTDeviceInternal addr;
} HcGattHdr;

#ifndef __clang__
_Static_assert(sizeof(HcGattHdr) == 8, "HcGattHdr is not 8 bytes in size");
#endif

typedef struct PACKED HcGattReadData {
  HcGattHdr hdr;
  uintptr_t context_ref;
  uint16_t att_handle;
} HcGattReadData;

typedef struct PACKED HcGattWriteData {
  HcGattHdr hdr;
  uintptr_t context_ref;
  uint16_t att_handle;
  uint16_t value_length;
  uint8_t value[];
} HcGattWriteData;

typedef struct PACKED HcGattReadRespData {
  HcGattHdr hdr;
  uintptr_t context_ref;
  BLEGATTError status;
  uint16_t att_handle;
  uint16_t value_length;
  uint8_t value[];
} HcGattReadRespData;

typedef struct PACKED HcGattWriteRespData {
  HcGattHdr hdr;
  uintptr_t context_ref;
  BLEGATTError status;
  uint16_t att_handle;
} HcGattWriteRespData;

typedef struct PACKED HcGattNotifIndicData {
  HcGattHdr hdr;
  uint16_t att_handle;
  uint16_t value_length;
  uint8_t value[];
} HcGattNotifIndicData;

// Handler on Host side
void hc_endpoint_gatt_handler_host(const HcProtocolMessage *msg);

// Handler on BT Controller side
void hc_endpoint_gatt_handler_controller(const HcProtocolMessage *msg);

// Functions called by Host
BTErrno hc_endpoint_gatt_read(const BTDeviceInternal *device, uint16_t att_handle, void *context);

BTErrno hc_endpoint_gatt_write(const BTDeviceInternal *device, uint16_t att_handle,
                               const uint8_t *value, uint16_t value_length, bool resp_required,
                               void *context);

// Functions called by BT Controller
void hc_endpoint_gatt_send_read_complete(const BTDeviceInternal *device, uint16_t handle,
    BLEGATTError status, uint16_t value_length, const uint8_t *value, uintptr_t context_ref);

void hc_endpoint_gatt_send_write_complete(const BTDeviceInternal *device, uint16_t handle,
                                          BLEGATTError status, uintptr_t context_ref);

void hc_endpoint_gatt_send_notification(const BTDeviceInternal *device, uint16_t handle,
                                        uint16_t value_length, const uint8_t *value);

void hc_endpoint_gatt_send_indication(const BTDeviceInternal *device, uint16_t handle,
                                      uint16_t value_length, const uint8_t *value);
