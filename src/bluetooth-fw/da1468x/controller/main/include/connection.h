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

#include "gatt_wrapper_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Connection Connection;
typedef struct BTDeviceInternal BTDeviceInternal;
typedef struct BTDeviceAddress BTDeviceAddress;
typedef struct BleConnectionParams BleConnectionParams;
typedef struct PPoGATTWorkAroundState PPoGATTWorkAroundState;
typedef void (*ConnectionForEachCallback)(Connection *connection, void *data);

// Call once on boot before doing anything with Bluetooth
void connection_module_init(void);

// Call every time a BT connection is created/destroyed. These ops should
// be driven from events received by the ble_task
//! @param initial_addr The remote address at the time of connection establishment. See notes
//! with the initial_addr field in the Connection struct.
//! @param local_addr The local/own address that was used to advertise at the time of connection
//! establishment.
Connection *connection_create(uint16_t conn_idx, const BTDeviceInternal *initial_addr,
                              const BTDeviceAddress *local_addr, const BleConnectionParams *params);
void connection_destroy(Connection *connection);

// Returns true if the pointer given is in our list of connections
bool connection_is_valid(Connection *connection);

// Enqueues a Gatt Operation to the list of outstanding Gatt Operations in the Connection object.
void connection_enqueue_gatt_op(Connection *connection, uintptr_t context_ref,
                                GattRespDest resp_dest, GattOpType op_type);
// Dequeues a Gatt Operation from the list of outstanding Gatt Operations in the Connection object.
// Returns true if the object was successfully dequeued and false if there are no operations known
// to be in progress
bool connection_dequeue_gatt_op(Connection *connection, uintptr_t *context_ref,
                                GattRespDest *resp_dest, GattOpType expected_op_type);
// Pops the most recent Gatt Operation appended to the list.
bool connection_pop_gatt_op(Connection *connection);

//
// Retrieve Connections
//

Connection *connection_by_idx(uint16_t conn_idx);
Connection *connection_by_idx_check(uint16_t conn_idx);
Connection *connection_by_address(const BTDeviceInternal *addr_buf);
Connection *connection_by_address_check(const BTDeviceInternal *addr_out);

// @note: internal lock will be held for the duration of this call
void connection_for_each(ConnectionForEachCallback cb, void *data);

//
// Getters
//

uint16_t connection_get_idx(Connection *connection);
//! If valid, gets the updated_addr of the connection, or otherwise the initial_addr.
void connection_get_address(const Connection *connection, BTDeviceInternal *addr_buf);
//! Gets the local/own address that was used to advertise at the time of connection establishment.
void connection_get_local_address(Connection *connection, BTDeviceAddress *addr_buf);
void connection_get_address_by_idx_check(uint16_t conn_idx, BTDeviceInternal *addr_out);
void connection_get_conn_params(const Connection *connection,
                                BleConnectionParams *params_out);
bool connection_is_subscribed_to_gatt_mtu_notifications(const Connection *connection);
bool connection_is_gateway(Connection *connection);
bool connection_is_subscribed_to_connection_status_notifications(const Connection *connection);
bool connection_is_subscribed_to_conn_param_notifications(const Connection *connection);
bool connection_should_pin_address(const Connection *connection);
bool connection_should_auto_accept_re_pairing(const Connection *connection);
bool connection_is_reversed_ppogatt_enabled(const Connection *connection);
uint8_t connection_get_last_pairing_result(uint16_t conn_idx);
PPoGATTWorkAroundState *connection_get_ppogatt_wa_state(Connection *connection);

//
// Setters - Sets the requested value provided connection_is_valid(connection) returns true
//

void connection_set_gateway(Connection *connection, bool is_gateway);
void connection_set_subscribed_to_connection_status_notifications(
    Connection *connection, bool is_subscribed);
void connection_set_subscribed_to_gatt_mtu_notifications(
    Connection *connection, bool is_subscribed);
void connection_set_subscribed_to_conn_param_notifications(
    Connection *connection, bool is_subscribed);
void connection_set_conn_params(Connection *connection, const BleConnectionParams *params);

void connection_update_address(Connection *connection, const BTDeviceInternal *updated_addr);
void connection_set_should_pin_address(Connection *connection, bool should_pin_address);
void connection_set_should_auto_accept_re_pairing(Connection *connection,
                                                  bool should_auto_accept_re_pairing);
void connection_set_reversed_ppogatt_enabled(Connection *connection,
                                             bool is_reversed_ppogatt_enabled);
void connection_set_last_pairing_result(uint16_t conn_idx, uint8_t result);
void connection_set_ppogatt_wa_state(Connection *connection, PPoGATTWorkAroundState *state);
