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

#include "gatt_wrapper.h"
#include "gatt_wrapper_types.h"

#include "connection.h"
#include "hc_protocol/hc_endpoint_gatt.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"
#include "system/passert.h"

#include "ble_gattc.h"

#include "rwble_hl_error.h"

#include <bluetooth/bluetooth_types.h>
#include <util/list.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static GattRespDest prv_dest_from_source(GattReqSource source) {
  return (source == GattReqSourceHost) ? GattRespDestHost : GattRespDestController;
}

static bool prv_enqueue(
    uint16_t conn_idx, uintptr_t context_ref, GattRespDest resp_dest, GattOpType op_type) {
  Connection *conn = connection_by_idx(conn_idx);
  if (!conn) {
    PBL_LOG(LOG_LEVEL_WARNING, "Failed to find connection during enqueue attempt");
    return false;
  }
  connection_enqueue_gatt_op(conn, context_ref, resp_dest, op_type);
  return true;
}

static bool prv_pop_errored_op(uint16_t conn_idx) {
  Connection *conn = connection_by_idx(conn_idx);
  if (!conn) {
    PBL_LOG(LOG_LEVEL_WARNING, "Failed to find connection during queue pop attempt");
    return false;
  }
  connection_pop_gatt_op(conn);
  return true;
}

static void prv_pop_if_failing_rv(uint16_t conn_idx, ble_error_t rv) {
  if (rv != BLE_STATUS_OK) {
    prv_pop_errored_op(conn_idx);
  }
}

ble_error_t gatt_wrapper_read(uint16_t conn_idx, uint16_t handle, uintptr_t context_ref,
                              GattReqSource source) {
  GATT_LOG_DEBUG("gatt_wrapper_read: handle: %d, context_ref: %d, source: %d",
                 handle, context_ref, source);

  const GattRespDest resp_dest = prv_dest_from_source(source);
  if (!prv_enqueue(conn_idx, context_ref, resp_dest, GattOpType_Read)) {
    return BLE_ERROR_NOT_CONNECTED;
  }
  ble_error_t rv = ble_gattc_read(conn_idx, handle, 0);
  prv_pop_if_failing_rv(conn_idx, rv);
  return rv;
}

ble_error_t gatt_wrapper_read_by_uuid(uint16_t conn_idx, uint16_t start_h, uint16_t end_h,
                                      const att_uuid_t *uuid, uintptr_t context_ref,
                                      GattReqSource source) {
  GATT_LOG_DEBUG("gatt_wrapper_read_uuid: context_ref: %d, source: %d", context_ref, source);

  const GattRespDest resp_dest = prv_dest_from_source(source);
  if (!prv_enqueue(conn_idx, context_ref, resp_dest, GattOpType_Read)) {
    return BLE_ERROR_NOT_CONNECTED;
  }
  ble_error_t rv = ble_gattc_read_by_uuid(conn_idx, start_h, end_h, uuid);
  prv_pop_if_failing_rv(conn_idx, rv);
  return rv;
}


ble_error_t gatt_wrapper_write(uint16_t conn_idx, uint16_t handle, uint16_t length,
                               const uint8_t *value, uintptr_t context_ref, GattReqSource source) {
  GATT_LOG_DEBUG("gatt_wrapper_write: handle: %d, context_ref: %d, source: %d",
                 handle, context_ref, source);

  const GattRespDest resp_dest = prv_dest_from_source(source);
  if (!prv_enqueue(conn_idx, context_ref, resp_dest, GattOpType_Write)) {
    return BLE_ERROR_NOT_CONNECTED;
  }
  ble_error_t rv = ble_gattc_write(conn_idx, handle, 0, length, value);
  prv_pop_if_failing_rv(conn_idx, rv);
  return rv;
}

ble_error_t gatt_wrapper_write_no_resp(uint16_t conn_idx, uint16_t handle, uint16_t length,
                                       const uint8_t *value) {
  GATT_LOG_DEBUG("gatt_wrapper_write_no_resp: handle: %d", handle);

  const GattRespDest resp_dest = GattRespDestNone;
  if (!prv_enqueue(conn_idx, 0, resp_dest, GattOpType_Write)) {
    return BLE_ERROR_NOT_CONNECTED;
  }
  const bool signed_write = false;
  ble_error_t rv = ble_gattc_write_no_resp(conn_idx, handle, signed_write, length, value);
  prv_pop_if_failing_rv(conn_idx, rv);
  return rv;
}

static GattRespDest prv_handle_gatt_event(uint16_t conn_idx, uint16_t handle,
                                          BTDeviceInternal *addr, uintptr_t *context_ref,
                                          BLEGATTError status, GattOpType expected_type) {
  // This used to be `connection_by_idx_check`, but due to PBL-36813, needed to change it.
  // The error that comes back in the `evt` is `GAP_ERR_COMMAND_DISALLOWED: 0x43`, but to make
  // sure we don't assert on any other future errors, we check for the connection.
  Connection *conn = connection_by_idx(conn_idx);
  if (!conn) {
    return GattRespDestNone;
  }
  GattRespDest resp_dest;

  if (!connection_dequeue_gatt_op(conn, context_ref, &resp_dest, expected_type)) {
    PBL_LOG(LOG_LEVEL_ALWAYS, "No gatt op to dequeue, status %d, hdl: %d", status, (int)handle);
    // FIXME: I think this happens if we reconnect too fast. The log above will get flushed to the
    // main MCU before the crash and should provide some extra context. I believe this state is
    // captured by GAP_ERR_COMMAND_DISALLOWED but let's assert if we catch another scenario
    PBL_ASSERTN(status == (int)GAP_ERR_COMMAND_DISALLOWED);
    return GattRespDestNone;
  }
  connection_get_address(conn, addr);
  return resp_dest;
}

void gatt_wrapper_handle_read_completed(const ble_evt_gattc_read_completed_t *evt) {
  BTDeviceInternal addr;
  uintptr_t context_ref;
  GATT_LOG_DEBUG("gatt_wrapper_handle_read_completed: handle: %d, status: %d",
                 evt->handle, evt->status);
  const BLEGATTError status = (BLEGATTError)evt->status;
  GattRespDest resp_dest = prv_handle_gatt_event(evt->conn_idx, evt->handle, &addr, &context_ref,
                                                 status, GattOpType_Read);


  switch (resp_dest) {
    case GattRespDestHost:
      hc_endpoint_gatt_send_read_complete(&addr, evt->handle, status, evt->length,
                                          &evt->value[0] + evt->offset, context_ref);
      break;
    case GattRespDestController:
      PBL_ASSERTN(context_ref);
      ((gatt_wrapper_read_cb) context_ref)(evt);
      break;
    default:
      break;
  }
}

void gatt_wrapper_handle_write_completed(const ble_evt_gattc_write_completed_t *evt) {
  BTDeviceInternal addr;
  uintptr_t context_ref;
  GATT_LOG_DEBUG("gatt_wrapper_handle_write_completed: handle: %d, status: %d",
                 evt->handle, evt->status);
  const BLEGATTError status = (BLEGATTError)evt->status;
  GattRespDest resp_dest = prv_handle_gatt_event(evt->conn_idx, evt->handle, &addr, &context_ref,
                                                 status, GattOpType_Write);

  switch (resp_dest) {
    case GattRespDestHost:
      hc_endpoint_gatt_send_write_complete(&addr, evt->handle, status, context_ref);
      break;
    case GattRespDestController:
      PBL_ASSERTN(context_ref);
      ((gatt_wrapper_write_cb) context_ref)(evt);
      break;
    default:
      break;
  }
}

void gatt_wrapper_handle_notification(const ble_evt_gattc_notification_t *evt) {
  BTDeviceInternal addr;
  Connection *conn = connection_by_idx_check(evt->conn_idx);
  connection_get_address(conn, &addr);
  hc_endpoint_gatt_send_notification(&addr, evt->handle, evt->length, evt->value);
}

void gatt_wrapper_handle_indication(const ble_evt_gattc_indication_t *evt) {
  BTDeviceInternal addr;
  Connection *conn = connection_by_idx_check(evt->conn_idx);
  connection_get_address(conn, &addr);
  hc_endpoint_gatt_send_indication(&addr, evt->handle, evt->length, evt->value);

  // GATT Indications are already automatically confirmed by the Dialog SDK.
  // See comments with ble_gattc_indication_cfm().
}
