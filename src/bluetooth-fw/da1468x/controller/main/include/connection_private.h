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

#include "connection.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gap_le_connect.h>
#include <util/list.h>

#include <stdint.h>

typedef struct GattOperation GattOperation;

typedef enum {
  ConnectionFlag_IsSubscribedToConnectionStatusNotifications = 0,
  ConnectionFlag_IsSubscribedToGattMtuNotifications,
  ConnectionFlag_IsSubscribedToConnParamNotifications,
  ConnectionFlag_ShouldPinAddress,
  //! @note The flag in the Connection struct is only relevant during the pairing process.
  //! Once bonded, the value gets stored in the bonding list (storage.c / device_t).
  ConnectionFlag_ShouldAutoAcceptRePairing,
  //! @note The flag in the Connection struct is only relevant during the pairing process.
  //! Once bonded, the value gets stored in the bonding list (storage.c / device_t).
  ConnectionFlag_IsReversedPPoGATTEnabled,
  ConnectionFlagCount,
} ConnectionFlag;

typedef struct Connection {
  ListNode node;

  uint16_t conn_idx;

  //! Remote address at the time the connection was established.
  //! This can be the actual connection address OR the resolved address.
  //! The former is the case for unbonded connections (including yet-to-be-bonded connections).
  //! The latter is the case for bonded reconnections: if we are bonded (have an IRK) and the
  //! underlying stack was able to resolve the address before passing the connection establishment
  //! event to ble_task.
  BTDeviceInternal initial_addr;

  //! Updated remote address. In case the address got resolved some time after connecting,
  //! for example after pairing happened, the resolved address will be stored in this field.
  //! The initial address will stay stored in initial_addr and the resolved address will be set in
  //! this field. For bonded reconnections, this field will not be used (remain all zeroes).
  bool has_updated_addr;
  BTDeviceInternal updated_addr;

  //! Local address at the time the connection was created.
  BTDeviceAddress local_addr;

  GattOperation *gatt_op_list;

  bool is_gateway;

  //! @see pebble_pairing_service.c
  uint32_t flags;
  BleConnectionParams conn_params;
  uint8_t last_pairing_result;

  //! @see ppogatt_emulated_server_wa.c
  PPoGATTWorkAroundState *ppogatt_wa_state;
} Connection;

_Static_assert((sizeof(((Connection *)0)->flags) * 8) >= ConnectionFlagCount,
               "Bitfield 'flags' full!");
