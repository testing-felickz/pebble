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

#include "pebble_pairing_service_impl.h"
#include "connection.h"
#include "hc_protocol/hc_endpoint_pebble_pairing_service.h"
#include "system/logging.h"
#include "system/passert.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/pebble_bt.h>
#include <bluetooth/pebble_pairing_service.h>
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

#define READ_RESPONSE_BUFFER_SIZE \
    (MAX(sizeof(PebblePairingServiceConnParamsReadNotif), \
         sizeof(PebblePairingServiceConnectivityStatus)))
#define ATT_DEFAULT_MTU (23)

typedef struct {
  ble_service_t svc;

  //! ATT handles
  struct {
    uint16_t conn_status;
    uint16_t conn_status_cccd;
    uint16_t trigger_pairing;
    uint16_t gatt_mtu;
    uint16_t gatt_mtu_cccd;
    uint16_t conn_params;
    uint16_t conn_params_cccd;
  } att_hdl;
} PebblePairingServiceCtx;

static PebblePairingServiceCtx s_pps_ctx;

static bool prv_is_encrypted(uint16_t conn_idx) {
  gap_sec_level_t level = GAP_SEC_LEVEL_1;
  ble_error_t e = ble_gap_get_sec_level(conn_idx, &level);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_get_sec_level: %u", e);
    return false;
  }
  return (level > GAP_SEC_LEVEL_1);
}

static void prv_set_gateway(Connection *connection, bool is_gateway) {
  const uint16_t conn_idx = connection_get_idx(connection);

  // Set for Connection
  connection_set_gateway(connection, is_gateway);

  // Set for `device_t`
  storage_acquire();
  device_t *dev = find_device_by_conn_idx(conn_idx);
  if (dev) {
    dev->is_gateway = is_gateway;
  }
  storage_release();

  BTDeviceInternal device;
  connection_get_address(connection, &device);
  hc_endpoint_pebble_pairing_service_found_gateway(&device);
}

static bool prv_is_bonded(uint16_t conn_idx) {
  storage_acquire();
  device_t *dev = find_device_by_conn_idx(conn_idx);
  const bool is_bonded = dev ? dev->bonded : false;
  storage_release();
  return is_bonded;
}

static void prv_device_foreach_search_gateway(const device_t *dev, void *ud) {
  bool *has_bonded_gateway = ud;
  if (dev->is_gateway) {
    *has_bonded_gateway = true;
  }
}

static bool prv_has_bonded_gateway(void) {
  bool has_bonded_gateway = false;

  storage_acquire();
  device_foreach(prv_device_foreach_search_gateway, &has_bonded_gateway);
  storage_release();

  return has_bonded_gateway;
}

extern bool ppogatt_emulated_server_wa_enabled(uint16_t conn_idx);
static void prv_get_connectivity_status(uint16_t conn_idx,
                                        PebblePairingServiceConnectivityStatus *status_out) {
  const bool is_encrypted = prv_is_encrypted(conn_idx);
  const bool is_bonded = prv_is_bonded(conn_idx);

  *status_out = (PebblePairingServiceConnectivityStatus) {
    .ble_is_encrypted = is_encrypted,
    .ble_is_bonded = is_bonded,
    .ble_is_connected = true,
    .has_bonded_gateway = prv_has_bonded_gateway(),
    .supports_pinning_without_security_request = true,
    .last_pairing_result = connection_get_last_pairing_result(conn_idx),
    .is_reversed_ppogatt_enabled = ppogatt_emulated_server_wa_enabled(conn_idx)
  };
}

static uint8_t prv_handle_connection_state_read(uint16_t conn_idx,
                                                uint8_t response_buf[READ_RESPONSE_BUFFER_SIZE]) {
  PebblePairingServiceConnectivityStatus status = {};
  prv_get_connectivity_status(conn_idx, &status);
  memcpy(response_buf, &status, sizeof(status));
  return sizeof(status);
}

static uint8_t prv_handle_gatt_mtu_read(uint16_t conn_idx,
                                        uint8_t response_buffer[READ_RESPONSE_BUFFER_SIZE]) {
  uint16_t mtu = ATT_DEFAULT_MTU;
  ble_error_t e = ble_gattc_get_mtu(conn_idx, &mtu);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gattc_get_mtu: %u", e);
  }
  memcpy(response_buffer, &mtu, sizeof(mtu));
  return sizeof(mtu);
}

static uint8_t prv_handle_conn_params_read(uint16_t conn_idx,
                                           uint8_t response_buffer[READ_RESPONSE_BUFFER_SIZE]) {
  Connection *connection = connection_by_idx_check(conn_idx);
  BleConnectionParams params;
  connection_get_conn_params(connection, &params);
  PebblePairingServiceConnParamsReadNotif *resp =
      (PebblePairingServiceConnParamsReadNotif *)response_buffer;
#if SUPPORTS_PACKET_LENGTH_EXTENSION
  resp->packet_length_extension_supported = true;
#else
  resp->packet_length_extension_supported = false;
#endif
  resp->rsvd = 0;
  resp->current_interval_1_25ms = params.conn_interval_1_25ms;
  resp->current_slave_latency_events = params.slave_latency_events;
  resp->current_supervision_timeout_10ms = params.supervision_timeout_10ms;
  return sizeof(PebblePairingServiceConnParamsReadNotif);
}

static void prv_handle_trigger_pairing(uint16_t conn_idx, const PairingTriggerRequestData *req) {
  PBL_LOG(LOG_LEVEL_DEBUG,
          "Trigger pairing! should_pin_address=%d, no_slave_security_request=%d, auto_acc=%d "
          "rev_ppogatt=%d",
          req->should_pin_address, req->no_slave_security_request,
          req->should_auto_accept_re_pairing,
          req->is_reversed_ppogatt_enabled);
  gap_sec_level_t level = GAP_SEC_LEVEL_3;
  if (prv_is_encrypted(conn_idx)) {
    PBL_LOG(LOG_LEVEL_INFO, "Link already encrypted!");
    if (!req->should_force_slave_security_request) {
      return;
    }
    // Make sure to request the same security level when sending a request to refresh encryption:
    ble_gap_get_sec_level(conn_idx, &level);
  }

  Connection *connection = connection_by_idx(conn_idx);
  connection_set_should_pin_address(connection, req->should_pin_address);
  connection_set_should_auto_accept_re_pairing(connection, req->should_auto_accept_re_pairing);
  connection_set_reversed_ppogatt_enabled(connection, req->is_reversed_ppogatt_enabled);

  if (!req->no_slave_security_request) {
    PBL_LOG(LOG_LEVEL_INFO, "Trying to establish security %u", level);
    ble_error_t e = ble_gap_set_sec_level(conn_idx, level);
    if (e != BLE_STATUS_OK) {
      PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_set_sec_level: %u", e);
    }
  }
}

static uint8_t prv_handle_handle_cccd_read(uint16_t handle, uint16_t conn_idx,
                                           uint8_t response_buffer[READ_RESPONSE_BUFFER_SIZE]) {
  Connection *connection = connection_by_idx_check(conn_idx);
  bool notifications_enabled = false;
  if (handle == s_pps_ctx.att_hdl.conn_status_cccd) {
    notifications_enabled = connection_is_subscribed_to_connection_status_notifications(connection);
  } else if (handle == s_pps_ctx.att_hdl.gatt_mtu_cccd) {
    notifications_enabled = connection_is_subscribed_to_gatt_mtu_notifications(connection);
  }
  const uint16_t cccd_value = notifications_enabled ? GATT_CCC_NOTIFICATIONS : 0;
  memcpy(response_buffer, &cccd_value, sizeof(cccd_value));
  return sizeof(cccd_value);
}

static bool prv_get_value(uint16_t conn_idx, uint16_t value_handle,
                          uint8_t response[READ_RESPONSE_BUFFER_SIZE],
                          size_t *response_length_out) {
  if (value_handle == s_pps_ctx.att_hdl.conn_status) {
    *response_length_out = prv_handle_connection_state_read(conn_idx, response);
  } else if (value_handle == s_pps_ctx.att_hdl.gatt_mtu) {
    *response_length_out = prv_handle_gatt_mtu_read(conn_idx, response);
  } else if (value_handle == s_pps_ctx.att_hdl.conn_params) {
    *response_length_out = prv_handle_conn_params_read(conn_idx, response);
  } else {
    return false;
  }
  return true;
}

static void prv_pps_handle_read_request(ble_service_t *svc, const ble_evt_gatts_read_req_t *evt) {
  uint8_t response[READ_RESPONSE_BUFFER_SIZE] = {};
  size_t response_length = 0;
  att_error_t response_status = ATT_ERROR_OK;

  if (evt->handle == s_pps_ctx.att_hdl.trigger_pairing) {
    const PairingTriggerRequestData request = {
      .should_pin_address = false,
      .no_slave_security_request = false,
    };
    prv_handle_trigger_pairing(evt->conn_idx, &request);
  } else if (evt->handle == s_pps_ctx.att_hdl.conn_status_cccd ||
             evt->handle == s_pps_ctx.att_hdl.gatt_mtu_cccd ||
             evt->handle == s_pps_ctx.att_hdl.conn_params_cccd) {
    response_length = prv_handle_handle_cccd_read(evt->handle, evt->conn_idx, response);
  } else if (!prv_get_value(evt->conn_idx, evt->handle, response, &response_length)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Reading unhandled handle: %u !", evt->handle);
    response_status = ATT_ERROR_INVALID_HANDLE;
  }

  ble_error_t e = ble_gatts_read_cfm(evt->conn_idx, evt->handle, response_status,
                                     response_length, response);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gatts_read_cfm: %u", e);
  }
}

static void prv_send_notification(uint16_t conn_idx, uint16_t value_handle) {
  uint8_t data[READ_RESPONSE_BUFFER_SIZE] = {};
  size_t length = 0;

  if (!prv_get_value(conn_idx, value_handle, data, &length)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unexpected handle! %u", value_handle);
    return;
  }

  ble_error_t e = ble_gatts_send_event(conn_idx, value_handle,
                                       GATT_EVENT_NOTIFICATION, length, data);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gatts_send_event: %u", e);
  }
}

static att_error_t prv_handle_cccd_write(const ble_evt_gatts_write_req_t *evt) {
  if (evt->length < sizeof(uint16_t)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Invalid CCCD value length %u!", evt->length);
    return ATT_ERROR_INVALID_VALUE_LENGTH;
  }
  uint16_t cccd_value = 0;
  memcpy(&cccd_value, evt->value, sizeof(cccd_value));
  bool is_subscribed = (cccd_value & GATT_CCC_NOTIFICATIONS);
  Connection *connection = connection_by_idx_check(evt->conn_idx);
  uint16_t value_handle;
  if (evt->handle == s_pps_ctx.att_hdl.conn_status_cccd) {
    connection_set_subscribed_to_connection_status_notifications(connection, is_subscribed);
    value_handle = s_pps_ctx.att_hdl.conn_status;
    // If the device has subscribed to this characteristic, we assume that it's the gateway or the
    // phone that is running the Pebble app:
    prv_set_gateway(connection, true);

    if (!is_subscribed) {
      // The CoreBluetooth central that the iOS app uses to subscribe to the Connection Status
      // characteristic does not use state restoration. Therefore, when the app crashes / is
      // jetsam'd, iOS will unsubscribe from the characteristic. Use this as a trigger to re-launch
      // the app:
      PBL_LOG(LOG_LEVEL_INFO, "Characteristic got unsubscribed, triggering app launch!");
      hc_endpoint_pebble_pairing_service_send_ios_app_termination_detected();
    }
  } else if (evt->handle == s_pps_ctx.att_hdl.gatt_mtu_cccd) {
    connection_set_subscribed_to_gatt_mtu_notifications(connection, is_subscribed);
    value_handle = s_pps_ctx.att_hdl.gatt_mtu;
  } else if (evt->handle == s_pps_ctx.att_hdl.conn_params_cccd) {
    connection_set_subscribed_to_conn_param_notifications(connection, is_subscribed);
    value_handle = s_pps_ctx.att_hdl.conn_params;
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "Unknown CCCD handle %u!", evt->handle);
    return ATT_ERROR_INVALID_HANDLE;
  }

  if (is_subscribed) {
    prv_send_notification(evt->conn_idx, value_handle);
  }

  return ATT_ERROR_OK;
}

static att_error_t prv_handle_mtu_value_write(const ble_evt_gatts_write_req_t *evt) {
  if (evt->length != sizeof(uint16_t)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Invalid GATT MTU size %u!", evt->length);
    return ATT_ERROR_INVALID_VALUE_LENGTH;
  }
  // https://pebbletechnology.atlassian.net/browse/PBL-34474:
  // we ought to change our local MTU according to the requested MTU size.
  // However, if we call ble_gap_mtu_size_set() here, we'd reset the ATT table... :(
  // For now, stick to the configured maximum and just re-negotiate:
  ble_error_t e = ble_gattc_exchange_mtu(evt->conn_idx);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gattc_exchange_mtu: %u", e);
    return ATT_ERROR_UNLIKELY;
  }
  return ATT_ERROR_OK;
}

static att_error_t prv_validate_set_remote_param_mgmt_settings_cmd(
    const PebblePairingServiceRemoteParamMgmtSettings *settings, size_t length) {
  if (length < sizeof(PebblePairingServiceRemoteParamMgmtSettings)) {
    return ATT_ERROR_INVALID_VALUE_LENGTH;
  }
  bool has_param_set =
      (length >= PEBBLE_PAIRING_SERVICE_REMOTE_PARAM_MGTM_SETTINGS_SIZE_WITH_PARAM_SETS);
  if (has_param_set) {
    for (int i = 0; i < NumResponseTimeState; ++i) {
      if (settings->connection_parameter_sets[i].interval_min_1_25ms < LL_CONN_INTV_MIN_SLOTS) {
        return (att_error_t)PebblePairingServiceGATTError_ConnParamsMinSlotsTooSmall;
      }
      if (settings->connection_parameter_sets[i].interval_min_1_25ms > LL_CONN_INTV_MAX_SLOTS) {
        return (att_error_t)PebblePairingServiceGATTError_ConnParamsMinSlotsTooLarge;
      }
      if (settings->connection_parameter_sets[i].interval_min_1_25ms +
          settings->connection_parameter_sets[i].interval_max_delta_1_25ms >
          LL_CONN_INTV_MAX_SLOTS) {
        return (att_error_t)PebblePairingServiceGATTError_ConnParamsMaxSlotsTooLarge;
      }
      if (settings->connection_parameter_sets[i].supervision_timeout_30ms * 30 <
          LL_SUPERVISION_TIMEOUT_MIN_MS) {
        return (att_error_t)PebblePairingServiceGATTError_ConnParamsSupervisionTimeoutTooSmall;
      }
    }
  }
  return ATT_ERROR_OK;
}

static att_error_t prv_validate_set_desired_state_cmd(
    const PebblePairingServiceRemoteDesiredState *desired_state, size_t length) {
  if (length < sizeof(PebblePairingServiceRemoteDesiredState)) {
    return ATT_ERROR_INVALID_VALUE_LENGTH;
  }
  if (desired_state->state >= NumResponseTimeState) {
    return (att_error_t)PebblePairingServiceGATTError_ConnParamsInvalidRemoteDesiredState;
  }
  return ATT_ERROR_OK;
}

static att_error_t prv_handle_ple_update_request(
    uint16_t conn_idx, const PebblePairingServicePacketLengthExtension *ple_req, size_t length) {
  if (length < sizeof(PebblePairingServicePacketLengthExtension)) {
    return ATT_ERROR_INVALID_VALUE_LENGTH;
  }

#if !SUPPORTS_PACKET_LENGTH_EXTENSION
  return ((att_error_t)PebblePairingServiceGATTError_DeviceDoesNotSupportPLE);
#else
  if (ple_req->trigger_ll_length_req) {
    extern void hci_initiate_length_change(uint16_t conn_idx);
    hci_initiate_length_change(conn_idx);
  } else {
    PBL_LOG(LOG_LEVEL_DEBUG, "PLE call a no-op");
  }

  return ATT_ERROR_OK;
#endif
}

extern void power_inhibit_sleep(void);

static att_error_t prv_handle_conn_params_write(const ble_evt_gatts_write_req_t *evt) {
  Connection *connection = connection_by_idx(evt->conn_idx);
  if (!connection) {
    return ATT_ERROR_UNLIKELY;
  }

  att_error_t rv;

  if (evt->length == 0) {
    rv = ATT_ERROR_INVALID_VALUE_LENGTH;
    goto finally;
  }

  const PebblePairingServiceConnParamsWrite *conn_params =
      (const PebblePairingServiceConnParamsWrite *)evt->value;

  const size_t length = (evt->length - offsetof(PebblePairingServiceConnParamsWrite,
                                                remote_desired_state));
  switch (conn_params->cmd) {
    case PebblePairingServiceConnParamsWriteCmd_SetRemoteParamMgmtSettings:
      rv = prv_validate_set_remote_param_mgmt_settings_cmd(&conn_params->remote_param_mgmt_settings,
                                                         length);
      break;
    case PebblePairingServiceConnParamsWriteCmd_SetRemoteDesiredState:
      rv = prv_validate_set_desired_state_cmd(&conn_params->remote_desired_state, length);
      break;
    case PebblePairingServiceConnParamsWriteCmd_EnablePacketLengthExtension:
      rv = prv_handle_ple_update_request(evt->conn_idx, &conn_params->ple_req, length);
      break;
    case PebblePairingServiceConnParamsWriteCmd_InhibitBLESleep:
      power_inhibit_sleep();
      rv = (length >= sizeof(PebblePairingServiceInhibitBLESleep)) ?
          ATT_ERROR_OK : ATT_ERROR_INVALID_VALUE_LENGTH;
      break;
    default:
      rv = (att_error_t)PebblePairingServiceGATTError_UnknownCommandID;
      break;
  }

finally:
  if (rv == ATT_ERROR_OK) {
    hc_endpoint_pebble_pairing_service_send_conn_params(connection, conn_params, evt->length);
  } else if (rv == ATT_ERROR_INVALID_VALUE_LENGTH) {
    PBL_LOG(LOG_LEVEL_ALWAYS, "Invalid value length %"PRIu16, evt->length);
  }
  return rv;
}

static void prv_pps_handle_write_request(ble_service_t *svc, const ble_evt_gatts_write_req_t *evt) {
  att_error_t rv = ATT_ERROR_INVALID_HANDLE;
  if (evt->handle == s_pps_ctx.att_hdl.trigger_pairing) {
    const PairingTriggerRequestData *pairing_trigger_request =
        (PairingTriggerRequestData *)evt->value;
    prv_handle_trigger_pairing(evt->conn_idx, pairing_trigger_request);
  } else if (evt->handle == s_pps_ctx.att_hdl.conn_status_cccd ||
             evt->handle == s_pps_ctx.att_hdl.gatt_mtu_cccd ||
             evt->handle == s_pps_ctx.att_hdl.conn_params_cccd) {
    rv = prv_handle_cccd_write(evt);
  } else if (evt->handle == s_pps_ctx.att_hdl.gatt_mtu) {
    rv = prv_handle_mtu_value_write(evt);
  } else if (evt->handle == s_pps_ctx.att_hdl.conn_params) {
    rv = prv_handle_conn_params_write(evt);
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "Writing unhandled handle: %u !", evt->handle);
  }

  ble_error_t e =  ble_gatts_write_cfm(evt->conn_idx, evt->handle, rv);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gatts_write_cfm: %u", e);
  }
}

void pebble_pairing_service_handle_status_change(Connection *connection, uint16_t conn_idx) {
  if (!connection_is_subscribed_to_connection_status_notifications(connection)) {
    return;
  }
  prv_send_notification(conn_idx, s_pps_ctx.att_hdl.conn_status);
}

void pebble_pairing_service_handle_gatt_mtu_change(Connection *connection, uint16_t conn_idx) {
  if (!connection_is_subscribed_to_gatt_mtu_notifications(connection)) {
    return;
  }
  prv_send_notification(conn_idx, s_pps_ctx.att_hdl.gatt_mtu);
}

void pebble_pairing_service_handle_conn_params_change(Connection *connection, uint16_t conn_idx) {
  if (!connection_is_subscribed_to_conn_param_notifications(connection)) {
    return;
  }
  prv_send_notification(conn_idx, s_pps_ctx.att_hdl.conn_params);
}

static void prv_convert_uuid_to_little_endian(const uint8_t *buf, att_uuid_t *uuid) {
  uint8_t *reverse_uuid = (uint8_t *)&UuidMakeFromLEBytes(buf);
  ble_uuid_from_buf(reverse_uuid, uuid);
}

static void prv_register(PebblePairingServiceCtx *ctx, uint16_t start_hdl) {
  *ctx = (PebblePairingServiceCtx) {
    .svc = {
      .read_req = prv_pps_handle_read_request,
      .write_req = prv_pps_handle_write_request,
    },
  };

  const uint16_t num_included_services = 0;
  const uint16_t num_characteristics = 4;
  const uint16_t num_descriptors = 3;
  const uint16_t num_attr = ble_gatts_get_num_attr(num_included_services, num_characteristics,
                                                   num_descriptors);

  att_uuid_t uuid;

  ble_uuid_create16(PEBBLE_BT_PAIRING_SERVICE_UUID_16BIT, &uuid);
  PBL_ASSERTN(ble_gatts_add_service(&uuid, GATT_SERVICE_PRIMARY, num_attr, start_hdl) == 0);

  // RivieraWaves caches values internally and only calls back once if we don't set this:
  const uint8_t enable_read_cb_flag = GATTS_FLAG_CHAR_READ_REQ;

  // Connectivity Status characteristic + CCCD:
  prv_convert_uuid_to_little_endian(
      (const uint8_t[]){PEBBLE_BT_PAIRING_SERVICE_CONNECTION_STATUS_UUID}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_READ | GATT_PROP_NOTIFY, ATT_PERM_READ,
                                           sizeof(PebblePairingServiceConnectivityStatus),
                                           enable_read_cb_flag, NULL,
                                           &ctx->att_hdl.conn_status) == BLE_STATUS_OK);

  ble_uuid_create16(UUID_GATT_CLIENT_CHAR_CONFIGURATION, &uuid);
  PBL_ASSERTN(ble_gatts_add_descriptor(&uuid, ATT_PERM_RW, sizeof(uint16_t), enable_read_cb_flag,
                                       &ctx->att_hdl.conn_status_cccd) == BLE_STATUS_OK);

  // Trigger Pairing characteristic:
  prv_convert_uuid_to_little_endian(
      (const uint8_t[]){PEBBLE_BT_PAIRING_SERVICE_TRIGGER_PAIRING_UUID}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_READ | GATT_PROP_WRITE, ATT_PERM_RW,
                                      sizeof(uint8_t), enable_read_cb_flag, NULL,
                                      &ctx->att_hdl.trigger_pairing) == BLE_STATUS_OK);

  // GATT MTU characteristic + CCCD:
  prv_convert_uuid_to_little_endian(
      (const uint8_t[]){PEBBLE_BT_PAIRING_SERVICE_GATT_MTU_UUID}, &uuid);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_READ | GATT_PROP_NOTIFY |
                                      GATT_PROP_WRITE,
                                      ATT_PERM_RW, sizeof(uint16_t), enable_read_cb_flag, NULL,
                                      &ctx->att_hdl.gatt_mtu) == BLE_STATUS_OK);

  ble_uuid_create16(UUID_GATT_CLIENT_CHAR_CONFIGURATION, &uuid);
  PBL_ASSERTN(ble_gatts_add_descriptor(&uuid, ATT_PERM_RW, sizeof(uint16_t), enable_read_cb_flag,
                                  &ctx->att_hdl.gatt_mtu_cccd) == BLE_STATUS_OK);

  // GATT MTU characteristic + CCCD:
  prv_convert_uuid_to_little_endian(
      (const uint8_t[]){PEBBLE_BT_PAIRING_SERVICE_CONNECTION_PARAMETERS_UUID}, &uuid);
  const size_t max_len = MAX(sizeof(PebblePairingServiceConnParamsReadNotif),
                             PEBBLE_PAIRING_SERVICE_CONN_PARAMS_WRITE_SIZE_WITH_PARAM_SETS);
  PBL_ASSERTN(ble_gatts_add_characteristic(&uuid, GATT_PROP_READ | GATT_PROP_NOTIFY |
                                           GATT_PROP_WRITE,
                                           ATT_PERM_RW, max_len, enable_read_cb_flag, NULL,
                                           &ctx->att_hdl.conn_params) == BLE_STATUS_OK);

  ble_uuid_create16(UUID_GATT_CLIENT_CHAR_CONFIGURATION, &uuid);
  PBL_ASSERTN(ble_gatts_add_descriptor(&uuid, ATT_PERM_RW, sizeof(uint16_t), enable_read_cb_flag,
                                       &ctx->att_hdl.conn_params_cccd) == BLE_STATUS_OK);

  // Register service with the stack and update the handle offsets to absolute handle values:
  PBL_ASSERTN(ble_gatts_register_service(&ctx->svc.start_h,
                                         &ctx->att_hdl.conn_status,
                                         &ctx->att_hdl.conn_status_cccd,
                                         &ctx->att_hdl.trigger_pairing,
                                         &ctx->att_hdl.gatt_mtu,
                                         &ctx->att_hdl.gatt_mtu_cccd,
                                         &ctx->att_hdl.conn_params,
                                         &ctx->att_hdl.conn_params_cccd,
                                         NULL) == BLE_STATUS_OK);

  ctx->svc.end_h = ctx->svc.start_h + num_attr;
  // If this assert gets hit, it means we've shuffled the ATT table around!
  PBL_ASSERTN(ctx->svc.start_h == start_hdl);
}

void pebble_pairing_service_init(void) {
  memset(&s_pps_ctx.svc, 0, sizeof(s_pps_ctx.svc));

  // Register for callbacks with the dispatcher -- this only needs to happen once
  ble_service_add(&s_pps_ctx.svc);
}

void pebble_pairing_service_register(uint16_t start_hdl) {
  prv_register(&s_pps_ctx, start_hdl);
}
