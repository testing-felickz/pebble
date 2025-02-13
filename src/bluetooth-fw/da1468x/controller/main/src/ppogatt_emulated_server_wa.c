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

#include "ppogatt_emulated_server_wa.h"

#include "ble_task.h"
#include "bonding_flags.h"
#include "connection.h"
#include "gatt_wrapper.h"
#include "hc_protocol/hc_endpoint_discovery.h"
#include "hc_protocol/hc_endpoint_gatt.h"
#include "hc_protocol/hc_protocol.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"
#include "system/logging.h"
#include "system/passert.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/pebble_bt.h>
#include <bluetooth/pebble_pairing_service.h>
#include <util/attributes.h>
#include <util/math.h>
#include <util/size.h>
#include <util/uuid.h>

// Dialog SDK

#include "ble_gap.h"
#include "ble_gatt.h"
#include "ble_gattc.h"
#include "ble_gatts.h"
#include "ble_service.h"
#include "ble_uuid.h"
#include "storage.h"

#include <inttypes.h>
#include <string.h>

// Hack Alert
//
// There are some really sad Android phones/versions out there where publishing an Gatt Server does
// not work. To workaround this issue, we need to switch to using the watch as the PPoGATT Server
// and the phone as the PPoGATT client. On the main MCU side of things, our abstraction assumes
// that PPoGATT will always be a client. In the interest of not shuffling around this abstraction
// days before we are supposed to ship a final PRF, we can just emulate that the PPoGATT Server
// exists on the remote to the main MCU. The PPoGATT protocol itself is pretty symmetric, so all we
// really need to do is emulate a couple responses and remap Gatt Writes with Gatt Notifications
//
// Note: As part of this workaround, we do need to publish a new service but I think this should be
// harmless to watches which do not need the workaround

// Not foolproof but chose a high service handle that is unlikely to be occupied by a different
// service on the phone.
#define EMULATED_PPOGATT_SERVICE_HANDLE_MSB 0xE0
#define EMULATED_PPOGATT_SERVICE_HANDLE_LSB 0x00
#define EMULATED_PPOGATT_SERVICE_HDL \
  ((EMULATED_PPOGATT_SERVICE_HANDLE_MSB) << 8 | (EMULATED_PPOGATT_SERVICE_HANDLE_LSB))

#define EMULATED_META_CHAR_OFFSET 0x02
#define EMULATED_DATA_CHAR_OFFSET 0x04
#define EMULATED_DATA_CCCD_OFFSET 0x05

#define EMULATED_DATA_CHAR_HANDLE (EMULATED_PPOGATT_SERVICE_HDL + EMULATED_DATA_CHAR_OFFSET)

#define EMULATED_PPOGATT_OFFSET 0xFF

// An emulated GATTService blob for a PPoGATT server which would usually be on the phone
static const uint8_t s_emulated_ppogatt_gatt_service[] = {
  // GATTService
  // UUID
  0x10, 0x00, 0x00, 0x00, 0x32, 0x8e, 0x0f, 0xbb, 0xc6, 0x42, 0x1a, 0xa6, 0x69, 0x9b, 0xda, 0xda,
  // Discovery Generation
  0x00,
  // RSVD
  0x00,
  // Size Bytes
  0x51, 0x00,
  // Att Handle
  EMULATED_PPOGATT_SERVICE_HANDLE_LSB, EMULATED_PPOGATT_SERVICE_HANDLE_MSB,
  // Num characteristics
  0x02,
  // Num Descriptors
  0x01,
  // RSVD
  0x00,
  //// GATTCharacteristic
  //// UUID
  0x10, 0x00, 0x00, 0x02, 0x32, 0x8e, 0x0f, 0xbb, 0xc6, 0x42, 0x1a, 0xa6, 0x69, 0x9b, 0xda, 0xda,
  //// Att Handle Offset
  EMULATED_META_CHAR_OFFSET,
  //// Properties
  0x02,
  //// Num Descriptors
  0x00,
  //// GATTCharacteristic
  0x10, 0x00, 0x00, 0x01, 0x32, 0x8e, 0x0f, 0xbb, 0xc6, 0x42, 0x1a, 0xa6, 0x69, 0x9b, 0xda, 0xda,
  //// Offset
  EMULATED_DATA_CHAR_OFFSET,
  //// Properties
  0x14,
  //// Num Descriptors
  0x01,
  ////// Data descriptor
  0x00, 0x00, 0x29, 0x02, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb,
  ///// Offset
  EMULATED_DATA_CCCD_OFFSET,
  0x00
};

typedef struct {
  ble_service_t svc;

  //! ATT handles
  struct {
    uint16_t data;
    uint16_t data_wr;
    uint16_t meta;
    uint16_t cccd;
  } att_hdl;
} PPoGattServiceCtx;

static PPoGattServiceCtx s_ppogatt_ctx;

// Copy/Pasta
typedef enum {
  PPoGATTSessionType_InferredFromUuid = 0x00,
  PPoGATTSessionType_Hybrid = 0x01,
  PPoGATTSessionTypeCount,
} PPoGATTSessionType;

typedef struct PACKED {
  uint8_t ppogatt_min_version;
  uint8_t ppogatt_max_version;
  Uuid app_uuid;
  PPoGATTSessionType pp_session_type:8;
} PPoGATTMetaV1;

typedef struct PPoGATTWorkAroundState {
  bool remote_did_subscribe;
  HcGattWriteRespData *pending_response_data;
  size_t pending_data_size;
  bool ppogatt_server_found_on_phone;
} PPoGATTWorkAroundState;

static PPoGATTWorkAroundState *prv_get_state(Connection *conn) {
  // We're accessing data that's stored in Connection and we're not having extra locks, make sure
  // we only access from a single task (ble_task):
  ble_task_assert_is_executing_on_ble_task();

  PPoGATTWorkAroundState *state = connection_get_ppogatt_wa_state(conn);
  if (!state) {
    state = (PPoGATTWorkAroundState *)kernel_zalloc_check(sizeof(PPoGATTWorkAroundState));
    connection_set_ppogatt_wa_state(conn, state);
  }
  return state;
}

void ppogatt_destroy_state(PPoGATTWorkAroundState *state) {
  if (state->pending_response_data) {
    kernel_free(state->pending_response_data);
  }
  kernel_free(state);
}

static bool prv_is_emulated_server_wa_requested(uint16_t conn_idx) {
  bool enabled = false;
  storage_acquire();
  device_t *dev = find_device_by_conn_idx(conn_idx);
  if (dev) {
    // Only enable the work-around if the bit is present in the bonding list, in other words, it
    // had been set *before pairing*. Don't look at the Connection flag, because this can be set
    // at any time. This way bad apps can't just write the PPS bit to enable the WA server after
    // pairing w/o the bit set.
    enabled = (dev->flags & BleBondingFlag_IsReversedPPoGATTEnabled);
  }
  storage_release();
  return enabled;
}

void ppogatt_emulated_notify_phone_ppogatt_server_found(Connection *conn) {
  if (prv_is_emulated_server_wa_requested(connection_get_idx(conn))) {
    PPoGATTWorkAroundState *state = prv_get_state(conn);
    state->ppogatt_server_found_on_phone = true;
  }
}

bool ppogatt_emulated_server_wa_enabled(uint16_t conn_idx) {
  if (!prv_is_emulated_server_wa_requested(conn_idx)) {
    return false;
  }

  // Disable the workaround if a PPoGATT Server was found on the phone. This will allow the phone
  // app to easily disable the WA if it decides it's no longer necessary (after something like an OS
  // update, for example)
  Connection *connection = connection_by_idx_check(conn_idx);
  if (prv_get_state(connection)->ppogatt_server_found_on_phone) {
    return false;
  }

  return true;
}

static void prv_build_write_complete_payload(const HcGattHdr *hdr, uint16_t att_hdl,
                                             PPoGATTWorkAroundState *state) {
  if (state->pending_response_data) {
    PBL_LOG(LOG_LEVEL_DEBUG, "WOAH");
    kernel_free(state->pending_response_data);
  }
  const uint32_t alloc_size = sizeof(HcGattWriteRespData);
  state->pending_data_size = alloc_size;
  HcGattWriteRespData *data = kernel_zalloc_check(alloc_size);
  *data = (HcGattWriteRespData) {
    .status = BLEGATTErrorSuccess,
    .hdr.addr = hdr->addr,
    .att_handle = att_hdl,
    .context_ref = ((HcGattReadData *)hdr)->context_ref,
  };
  state->pending_response_data = data;
}

static void prv_enqueue_write_complete_if_ready(PPoGATTWorkAroundState *state) {
  if (!state->remote_did_subscribe) {
    return; // Gotta wait for remote to subscribe.
  }
  if (!state->pending_response_data) {
    return; // Gotta wait for FW to subscribe / write the emulated CCCD.
  }

  PBL_LOG(LOG_LEVEL_ALWAYS, "Phone & Watch have hit CCCD, initiate PPoGatt WA!");
  hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, HcMessageID_Gatt_WriteCompleted,
                                   (uint8_t *)state->pending_response_data,
                                   state->pending_data_size);
  kernel_free(state->pending_response_data);
  state->pending_response_data = NULL;
}

static void prv_handle_cccd_write(Connection *conn, bool subscribed) {
  PPoGATTWorkAroundState *state = prv_get_state(conn);
  state->remote_did_subscribe = subscribed;
  prv_enqueue_write_complete_if_ready(state);
}

static void prv_ppogatt_handle_write_request(ble_service_t *svc,
                                             const ble_evt_gatts_write_req_t *evt) {
  // Dialog requires this even if its a Write without response:
  ble_gatts_write_cfm(evt->conn_idx, evt->handle, ATT_ERROR_OK);

  if (!ppogatt_emulated_server_wa_enabled(evt->conn_idx)) {
    return;
  }

  BTDeviceInternal addr;
  Connection *conn = connection_by_idx_check(evt->conn_idx);
  if ((evt->handle == s_ppogatt_ctx.att_hdl.data) ||
      (evt->handle == s_ppogatt_ctx.att_hdl.data_wr)) {
    // Pretend the write request is actually a notification and fwd that to the MCU!
    connection_get_address(conn, &addr);
    hc_endpoint_gatt_send_notification(&addr, EMULATED_DATA_CHAR_HANDLE, evt->length, evt->value);
  } else if (evt->handle == s_ppogatt_ctx.att_hdl.cccd) {
    if (evt->offset == 0) {
      uint16_t value;
      memcpy(&value, evt->value, sizeof(value));
      bool subscribed = (GATT_CCC_NOTIFICATIONS == value);
      prv_handle_cccd_write(conn, subscribed);
      PBL_LOG(LOG_LEVEL_DEBUG, "Wrote reversed PPoGATT CCCD: %"PRIu16, subscribed);
    } else {
      PBL_LOG(LOG_LEVEL_DEBUG, "Invalid CCCD write offset %"PRIu16, evt->offset);
    }
  }
}

static void prv_ppogatt_handle_read_request(ble_service_t *svc,
                                            const ble_evt_gatts_read_req_t *evt) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Unexpected PPoGATT Service Read: 0x%"PRIx16, evt->handle);

  // In the future, we might actually return something useful. For now, just uncoditionally
  // respond to everything with the same payload
  uint8_t response = 0xAA;
  ble_error_t e = ble_gatts_read_cfm(evt->conn_idx, evt->handle, ATT_ERROR_OK,
                                     sizeof(response), &response);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gatts_read_cfm: %u", e);
  }
}

static bool prv_handle_emulated_meta_read(const HcProtocolMessage *msg) {
  PBL_ASSERTN(msg->command_id == HcMessageID_Gatt_Read);
  HcGattHdr *hdr = (HcGattHdr *)&msg->payload[0];

  PBL_LOG(LOG_LEVEL_DEBUG, "MR");

  // First ack the read request itself
  BTErrno rv = BTErrnoOK;
  hc_protocol_enqueue_response(msg, (uint8_t *)&rv, sizeof(rv)); // ack the read


  // Assume an app using this emulated workaround is Android and supports V1
  PPoGATTMetaV1 emulated_result = {
    .ppogatt_min_version = 1,
    .ppogatt_max_version = 1,
    .pp_session_type = PPoGATTSessionType_Hybrid
  };

  const uint32_t alloc_size = sizeof(HcGattReadRespData) + sizeof(emulated_result);
  HcGattReadRespData *data = kernel_zalloc_check(alloc_size);
  *data = (HcGattReadRespData) {
    .status = BLEGATTErrorSuccess,
    .hdr.addr = hdr->addr,
    .att_handle = ((HcGattReadData *)hdr)->att_handle,
    .value_length = sizeof(emulated_result),
    .context_ref = ((HcGattReadData *)hdr)->context_ref
  };
  memcpy(data->value, &emulated_result, sizeof(emulated_result));

  hc_protocol_enqueue_with_payload(HcEndpointID_Gatt, HcMessageID_Gatt_ReadCompleted,
                                   (uint8_t *)data, alloc_size);

  kernel_free(data);
  return true;
}

static bool prv_handle_emulated_cccd_write(const HcProtocolMessage *msg,
                                           PPoGATTWorkAroundState *state) {
  PBL_ASSERTN(msg->command_id == HcMessageID_Gatt_Write);
  HcGattHdr *hdr = (HcGattHdr *)&msg->payload[0];

  BTErrno rv = BTErrnoOK;
  hc_protocol_enqueue_response(msg, (uint8_t *)&rv, sizeof(rv)); // ack the write

  prv_build_write_complete_payload(hdr, ((HcGattWriteData *)hdr)->att_handle, state);
  prv_enqueue_write_complete_if_ready(state);

  return true;
}

static bool prv_handle_emulated_write(uint16_t conn_idx, const HcProtocolMessage *msg) {
  PBL_ASSERTN(msg->command_id == HcMessageID_Gatt_WriteNoResponse);

  // Remap a data write to a data notification
  HcGattWriteData *data = (HcGattWriteData *)&msg->payload[0];
  ble_error_t e = ble_gatts_send_event(
      conn_idx, s_ppogatt_ctx.att_hdl.data, GATT_EVENT_NOTIFICATION, data->value_length,
      &data->value[0]);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Emulated write failed: %d", (int)e);
  }

  return true;
}

bool ppogatt_emulated_server_handle_msg(uint16_t conn_idx, Connection *conn,
                                        const HcProtocolMessage *msg) {
  if (!ppogatt_emulated_server_wa_enabled(conn_idx)) {
    return false;
  }

  HcGattHdr *hdr = (HcGattHdr *)&msg->payload[0];
  uint16_t att_hdl = (msg->command_id == HcMessageID_Gatt_Read) ?
      ((HcGattReadData *)hdr)->att_handle : ((HcGattWriteData *)hdr)->att_handle;

  if (att_hdl < EMULATED_PPOGATT_SERVICE_HDL) {
    goto unhandled;
  }
  uint8_t offset = att_hdl - EMULATED_PPOGATT_SERVICE_HDL;

  switch (offset) {
    case EMULATED_META_CHAR_OFFSET:
      return prv_handle_emulated_meta_read(msg);
    case EMULATED_DATA_CHAR_OFFSET:
      return prv_handle_emulated_write(conn_idx, msg);
    case EMULATED_DATA_CCCD_OFFSET: {
      PPoGATTWorkAroundState *state = prv_get_state(conn);
      return prv_handle_emulated_cccd_write(msg, state);
    }
    default:
      goto unhandled;
  }

unhandled:
  return false;
}

void ppogatt_inject_emulated_ppogatt_service_if_needed(uint16_t conn_idx) {
  if (!ppogatt_emulated_server_wa_enabled(conn_idx)) {
    return;
  }

  uint32_t payload_size = sizeof(BTDeviceInternal) + sizeof(s_emulated_ppogatt_gatt_service);
  HcProtocolDiscoveryServiceFoundPayload *payload = kernel_zalloc_check(payload_size);

  Connection *connection = connection_by_idx_check(conn_idx);
  connection_get_address(connection, &payload->address);

  memcpy(&payload->service, &s_emulated_ppogatt_gatt_service,
         sizeof(s_emulated_ppogatt_gatt_service));
  hc_endpoint_discovery_send_service_found(payload, payload_size);

  kernel_free(payload);
}

//
// Real PPoGATT Service Registration Code
//

// Yes, a straight copy from pebble_pairing_service.c
static void prv_convert_uuid_to_little_endian(const uint8_t *buf, att_uuid_t *uuid) {
  uint8_t *reverse_uuid = (uint8_t *)&UuidMakeFromLEBytes(buf);
  ble_uuid_from_buf(reverse_uuid, uuid);
}

void ppogatt_service_init(void) {
  memset(&s_ppogatt_ctx.svc, 0, sizeof(s_ppogatt_ctx.svc));

  // Register for callbacks with the dispatcher -- this only needs to happen once
  ble_service_add(&s_ppogatt_ctx.svc);
}

void ppogatt_service_register(uint16_t start_hdl) {
  PPoGattServiceCtx *ctx = &s_ppogatt_ctx;

  *ctx = (PPoGattServiceCtx) {
    .svc = {
      .read_req = prv_ppogatt_handle_read_request,
      .write_req = prv_ppogatt_handle_write_request,
    },
  };

  att_uuid_t uuid;

  const uint16_t num_included_services = 0;
  const uint16_t num_characteristics = 3;
  const uint16_t num_descriptors = 1;
  const uint16_t num_attr = ble_gatts_get_num_attr(num_included_services, num_characteristics,
                                                   num_descriptors);

  // RivieraWaves caches values internally and only calls back once if we don't set this:
  const uint8_t enable_read_cb_flag = GATTS_FLAG_CHAR_READ_REQ;

  // Just use the max allowed size, no buffers get allocated because of GATTS_FLAG_CHAR_READ_REQ.
  const uint16_t data_wr_size = 512;

  prv_convert_uuid_to_little_endian((const uint8_t[]){PEBBLE_BT_UUID_EXPAND(
      PEBBLE_BT_PPOGATT_WATCH_SERVER_SERVICE_UUID_32BIT)}, &uuid);
  PBL_ASSERTN(ble_gatts_add_service(&uuid, GATT_SERVICE_PRIMARY, num_attr, start_hdl) == 0);

  prv_convert_uuid_to_little_endian((const uint8_t[]){PEBBLE_BT_UUID_EXPAND(
      PEBBLE_BT_PPOGATT_WATCH_SERVER_DATA_CHARACTERISTIC_UUID_32BIT)}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_WRITE_NO_RESP | GATT_PROP_NOTIFY,
                                           ATT_PERM_WRITE,
                                           data_wr_size,
                                           enable_read_cb_flag, NULL,
                                           &ctx->att_hdl.data) == BLE_STATUS_OK);

  ble_uuid_create16(UUID_GATT_CLIENT_CHAR_CONFIGURATION, &uuid);
  PBL_ASSERTN(ble_gatts_add_descriptor(&uuid, ATT_PERM_RW, sizeof(uint16_t), enable_read_cb_flag,
                                       &ctx->att_hdl.cccd) == BLE_STATUS_OK);

  prv_convert_uuid_to_little_endian((const uint8_t[]){PEBBLE_BT_UUID_EXPAND(
      PEBBLE_BT_PPOGATT_WATCH_SERVER_META_CHARACTERISTIC_UUID_32BIT)}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_READ, ATT_PERM_READ,
                                           sizeof(PPoGATTMetaV1),
                                           enable_read_cb_flag, NULL,
                                           &ctx->att_hdl.meta) == BLE_STATUS_OK);

  prv_convert_uuid_to_little_endian((const uint8_t[]){PEBBLE_BT_UUID_EXPAND(
      PEBBLE_BT_PPOGATT_WATCH_SERVER_DATA_WR_CHARACTERISTIC_UUID_32BIT)}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_WRITE_NO_RESP, ATT_PERM_WRITE,
                                           data_wr_size,
                                           enable_read_cb_flag, NULL,
                                           &ctx->att_hdl.data_wr) == BLE_STATUS_OK);


  PBL_ASSERTN(ble_gatts_register_service(&ctx->svc.start_h,
                                         &ctx->att_hdl.data,
                                         &ctx->att_hdl.cccd,
                                         &ctx->att_hdl.meta,
                                         &ctx->att_hdl.data_wr,
                                         NULL) == BLE_STATUS_OK);

  ctx->svc.end_h = ctx->svc.start_h + num_attr;

  PBL_ASSERTN(ctx->svc.start_h == start_hdl);
}
