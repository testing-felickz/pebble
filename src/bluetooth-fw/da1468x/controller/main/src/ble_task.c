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

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ble_gap.h>

#include "advert.h"
#include "dialog_analytics/analytics.h"
#include "bonding_flags.h"
#include "bonding_sync_impl.h"
#include "connection.h"
#include "debug_reboot_reason.h"
#include "dialog_utils.h"
#include "gatt_local_services.h"
#include "hci_rom_passthrough.h"
#include "host_transport.h"
#include "host_transport_impl.h"
#include "local_addr_impl.h"
#include "pebble_pairing_service_impl.h"
#include "system/hexdump.h"
#include "system/logging.h"
#include "system/passert.h"
#include "tasks.h"
#include "hc_protocol/hc_endpoint_analytics.h"
#include "hc_protocol/hc_endpoint_responsiveness.h"
#include "hc_protocol/hc_endpoint_gap_le_connect.h"
#include "hc_protocol/hc_endpoint_gap_service.h"
#include "hc_protocol/hc_endpoint_gatt.h"
#include "hc_protocol/hc_endpoint_pairing.h"
#include "gatt_client_discovery.h"
#include "gatt_wrapper.h"

// Dialog SDK:
#include "ad_ble.h"
#include "ble_att.h"
#include "ble_common.h"
#include "ble_gap.h"
#include "ble_gattc.h"
#include "ble_gatts.h"
#include "ble_mgr.h"
#include "ble_mgr_irb_common.h"
#include "ble_service.h"
#include "ble_uuid.h"
#include "osal.h"
#include "smp_common.h"
#include "storage.h"
#include "sys_watchdog.h"
#include "timers.h"

#include <FreeRTOS.h>
#include <semphr.h>

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gap_le_connect.h>
#include <bluetooth/gatt_service_types.h>
#include <bluetooth/hci_types.h>
#include <bluetooth/init.h>
#include <bluetooth/mtu.h>
#include <bluetooth/sm_types.h>

#include <util/uuid.h>

#define mainBLE_PERIPHERAL_TASK_PRIORITY              (tskIDLE_PRIORITY + 1)

///////////////////////////////////////////////////////////////////////////////////////////////////
// Defines to configure the different test scenarios (enable only one)


///////////////////////////////////////////////////////////////////////////////////////////////////
#define ADV_MAX_INTVL_SLOTS (400)

extern void test_hci_passthrough(void);
#define TEST_HCI_ROM_PASSTHROUGH (0)

typedef struct {
  SemaphoreHandle_t semph;
  const BTDriverConfig *config;
} BleTaskInitInfo;

/*
 * Main code
 */

typedef struct {
  uint16_t conn_idx; //! The connection that has the demo server

  uint16_t write_hdl;
  uint16_t write_cccd_hdl;

  uint16_t notify_hdl;
  uint16_t notify_cccd_hdl;

  uint32_t expected_read_header; //! the payload index we are expecting to receiver from the server
  uint32_t current_write_header; //! the current header we are writing to App
} demo_server_hdl_t;

static int8_t s_ble_perip_wdog_id;

static void prv_log_conn_params(const gap_conn_params_t *conn_params) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Current conn params: intvl min: %d max: %d latency: %d STO: %d",
          (int)conn_params->interval_min, (int)conn_params->interval_max,
          (int)conn_params->slave_latency, (int)conn_params->sup_timeout);
}

static bool prv_get_device_and_irk_by_conn_idx(uint16_t conn_idx,
                                               bool *is_resolved_out,
                                               SMIdentityResolvingKey *irk_out) {
  bool success = true;
  storage_acquire();
  {
    device_t *dev = find_device_by_conn_idx(conn_idx);
    if (!dev) {
      success = false;
      goto release;
    }
    bool is_resolved = false;
    if (dev && dev->irk) {
      is_resolved = true;
      if (irk_out) {
        memcpy(irk_out, dev->irk->key, sizeof(*irk_out));
      }
    } else {
      PBL_LOG(LOG_LEVEL_DEBUG, "Address not resolved");
    }
    if (is_resolved_out) {
      *is_resolved_out = is_resolved;
    }
  }
release:
  storage_release();
  return success;
}

static void handle_evt_gap_connected(ble_evt_gap_connected_t *evt) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Connected, idx=%d!", (int)evt->conn_idx);
  prv_log_conn_params(&evt->conn_params);

  if (!evt->is_master) {
    // To be compliant with BT Core Spec section 7.8.9, stop advertising when the chip gets
    // connected to. (The host expectes advertisements to have stopped in this scenario (see
    // gap_le_advert_handle_connect_as_slave)) Ideally we would handle this in our advert.c wrapper
    // but we don't have enough state available to conclude this in advert_handle_completed()
    advert_disable();
  }

  BTDeviceInternal addr;
  dialog_utils_bd_address_to_bt_device(&evt->peer_address, &addr);

  HcGapLeConnectionData event = {
    .connection_complete_event = {
      .conn_params = {
        .conn_interval_1_25ms = evt->conn_params.interval_min,
        .slave_latency_events = evt->conn_params.slave_latency,
        .supervision_timeout_10ms = evt->conn_params.sup_timeout,
      },
      .peer_address = addr,
      .status = HciStatusCode_Success,
      .is_master = evt->is_master,
      .handle = evt->conn_idx,
    },
  };

  // Store the current local address with the connection. It's possible the local address will
  // cycle while being connected. If we're going to pair and the device wants to use address
  // pinning, we need to make sure to pin the current address and not a newly generated one.
  const BTDeviceAddress *own_addr = (const BTDeviceAddress *)evt->own_addr.addr;
  const BleConnectionParams *params = &event.connection_complete_event.conn_params;
  connection_create(evt->conn_idx, &addr, own_addr, params);

  ble_error_t rv = ble_gattc_get_mtu(evt->conn_idx, &event.mtu);
  if (rv != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_WARNING, "ble_gattc_get_mtu failed with status: %d", rv);
  }

  // If the address got resolved, copy the IRK into the event:
  BleConnectionCompleteEvent *conn_evt = &event.connection_complete_event;
  prv_get_device_and_irk_by_conn_idx(evt->conn_idx, &conn_evt->is_resolved, &conn_evt->irk);

  hc_endpoint_gap_le_connect_send_connection_complete(&event);
}

static void prv_handle_conn_param_update_request_response(
    const ble_evt_gap_conn_param_update_completed_t *event) {
  // This callback getting invoked means that conn param request was acknowledged. Could be either
  // successful or not. If a failure, we return the failure up to the host
  if (event->status == BLE_STATUS_OK) {
    // We'll be getting an conn_param_update very soon, no need to alert the host.
    return;
  }

  PBL_LOG(LOG_LEVEL_WARNING, "Connection parameter update request failed with status: %d",
          event->status);

  Connection *conn = connection_by_idx(event->conn_idx);
  if (conn == NULL) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection for idx=%d", event->conn_idx);
    return;
  }

  BTDeviceInternal addr;
  connection_get_address(conn, &addr);

  hc_endpoint_responsiveness_notify_update(NULL, &addr, HciStatusCode_VS_Base + event->status);
}

static void prv_handle_conn_param_update_request(const ble_evt_gap_conn_param_update_req_t *evt) {
  gap_conn_params_t conn_params = evt->conn_params;
  PBL_LOG(LOG_LEVEL_DEBUG, "Master requesting conn param change, Conn Idx: %d - (%d %d %d %d)",
          evt->conn_idx, (int)conn_params.interval_min, (int)conn_params.interval_max,
          (int)conn_params.slave_latency, (int)conn_params.sup_timeout);

  // accept the change
  ble_gap_conn_param_update_reply(evt->conn_idx, true);
}

static void prv_handle_conn_param_update(const ble_evt_gap_conn_param_updated_t *event) {
  // This callback getting invoked means that conn param request was successful
  // Note: If this cb is not invoked within 30s after issuing an update request
  // we will be auto-disconnected (according to the ble_gap_conn_param_update
  // API)
  prv_log_conn_params(&event->conn_params);

  const uint16_t conn_idx = event->conn_idx;
  Connection *conn = connection_by_idx(conn_idx);
  if (conn == NULL) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection for idx=%d", conn_idx);
    return;
  }

  BTDeviceInternal addr;
  connection_get_address(conn, &addr);

  BleConnectionParams params = {
    .conn_interval_1_25ms = event->conn_params.interval_min,
    .slave_latency_events = event->conn_params.slave_latency,
    .supervision_timeout_10ms = event->conn_params.sup_timeout,
  };
  connection_set_conn_params(conn, &params);
  pebble_pairing_service_handle_conn_params_change(conn, conn_idx);
  hc_endpoint_responsiveness_notify_update(&params, &addr, HciStatusCode_Success);
}

static void prv_handle_evt_gap_disconnected(const ble_evt_gap_disconnected_t *evt) {
  PBL_LOG(LOG_LEVEL_INFO, "Disconnected: idx=%"PRIu16", reason=0x%"PRIx8,
          evt->conn_idx, evt->reason);
  local_addr_handle_disconnection();

  Connection *conn = connection_by_idx_check(evt->conn_idx);

  BTDeviceInternal addr;
  connection_get_address(conn, &addr);

  BleDisconnectionCompleteEvent event = {
    .peer_address = addr,
    .status = HciStatusCode_Success,
    .reason = evt->reason,
    .handle = evt->conn_idx,
  };

  hc_endpoint_gap_le_connect_send_disconnection_complete(&event);

  connection_destroy(connection_by_idx(evt->conn_idx));
}

static void prv_handle_evt_gap_sec_level_changed(const ble_evt_gap_sec_level_changed_t *evt) {
  PBL_LOG(LOG_LEVEL_INFO, "Security level changed to: %u", evt->level);
  Connection *conn = connection_by_idx(evt->conn_idx);
  if (conn == NULL) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection for idx=%d", evt->conn_idx);
    return;
  }

  pebble_pairing_service_handle_status_change(conn, evt->conn_idx);

  BTDeviceInternal addr;
  connection_get_address(conn, &addr);

  BleEncryptionChange event = {
    .dev_address = addr.address,
    .status = HciStatusCode_Success,
    .encryption_enabled = (evt->level > GAP_SEC_LEVEL_1),
  };

  hc_endpoint_gap_le_connect_send_encryption_changed(&event);
}

//! @return True iff there is an existing pairing for this connection with the
//! "BleBondingFlag_ShouldAutoAcceptRePairing" bit set AND the bit has been set again through the
//! Trigger Pairing characteristic. The second part of the condition may not be true in case a user
//! updated the phone to an Android version that does not have the bug (PBL-39369).
static bool prv_is_auto_accept_repairing_mode(Connection *conn, uint16_t conn_idx) {
  bool should_auto_accept_re_pairing;
  storage_acquire();
  {
    const device_t *dev = find_device_by_conn_idx(conn_idx);
    should_auto_accept_re_pairing = (dev &&
        (dev->flags & BleBondingFlag_ShouldAutoAcceptRePairing));
  }
  storage_release();
  should_auto_accept_re_pairing &= connection_should_auto_accept_re_pairing(conn);

  return should_auto_accept_re_pairing;
}

static void prv_handle_pairing_request(const ble_evt_gap_pair_req_t *evt) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Received pairing request.");

  const uint16_t conn_idx = evt->conn_idx;
  Connection *conn = connection_by_idx(conn_idx);
  if (conn == NULL) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection for idx=%d", conn_idx);
    return;
  }

  BTDeviceInternal device = {};
  connection_get_address(conn, &device);

  if (prv_is_auto_accept_repairing_mode(conn, conn_idx)) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Auto-accepting pairing request");
    extern void pair_reply(uint16_t conn_idx, bool is_confirmed);
    pair_reply(conn_idx, true);
    return;
  }

  hc_endpoint_pairing_send_pairing_request(&device);
}

static void prv_handle_address_resolved(const ble_evt_gap_address_resolved_t *evt) {
  PBL_LOG(LOG_LEVEL_DEBUG, "IRK exchanged and address resolved/updated.");
  // Update the IRK + Address (Dialog SDK swaps the connection address for the identity address,
  // even if the pairing process failed mid-way):
  BleAddressAndIRKChange e = {};
  if (!prv_get_device_and_irk_by_conn_idx(evt->conn_idx, &e.is_resolved, &e.irk)) {
    // Disconnected in the mean time
    return;
  }

  e.is_address_updated = true;
  dialog_utils_bd_address_to_bt_device(&evt->address, &e.device);
  dialog_utils_bd_address_to_bt_device(&evt->resolved_address, &e.new_device);

  // Also update the new address in the local connection list:
  Connection *connection = connection_by_idx_check(evt->conn_idx);
  connection_update_address(connection, &e.new_device);

  hc_endpoint_gap_le_connect_send_address_and_irk_changed(&e);
}

static void prv_handle_pairing_completed(const ble_evt_gap_pair_completed_t *evt) {
  PBL_LOG(LOG_LEVEL_INFO, "Pairing completed. Bond=%u, MITM=%u, status=0x%"PRIx8,
          evt->bond, evt->mitm, evt->status);
  const uint16_t conn_idx = evt->conn_idx;
  Connection *conn = connection_by_idx(conn_idx);
  if (conn == NULL) {
    PBL_LOG(LOG_LEVEL_ERROR, "No connection for idx=%d", conn_idx);
    return;
  }

  if (prv_is_auto_accept_repairing_mode(conn, conn_idx)) {
    // Don't sync the new pairing, we're dealing with an Android device that has to re-pair
    // upon every reconnection. Don't sync to avoid re-writing the pairing info upon every
    // reconnection. @see https://pebbletechnology.atlassian.net/browse/PBL-39369
    PBL_LOG(LOG_LEVEL_DEBUG, "Skipping syncing new pairing info...");
  } else {
    const bool success = (evt->status == BLE_STATUS_OK);
    if (success && evt->bond) {
      // Sync the new bonding:
      bonding_sync_handle_pairing_completed(conn, conn_idx);
    }
    BTDeviceInternal device = {};
    connection_get_address(conn, &device);
    hc_endpoint_pairing_send_pairing_complete(&device, evt->status);
  }

  // Convert to BT Spec error codes and update Pebble Pairing Service's Conn Status characteristic:
  const uint8_t smp_status = SMP_GET_PAIR_FAIL_REASON(evt->status);
  connection_set_last_pairing_result(conn_idx, smp_status);
  pebble_pairing_service_handle_status_change(conn, conn_idx);
}

static void prv_handle_gatt_mtu_changed(const ble_evt_gattc_mtu_changed_t *event) {
  Connection *connection = connection_by_idx_check(event->conn_idx);
  if (!connection) {
    PBL_LOG(LOG_LEVEL_WARNING, "No connection for idx %d", event->conn_idx);
    return;
  }

  PBL_LOG(LOG_LEVEL_INFO, "MTU updated: %"PRIu16, event->mtu);
  pebble_pairing_service_handle_gatt_mtu_change(connection, event->conn_idx);
  hc_endpoint_gap_service_mtu_changed(connection, event->mtu);
}

static void prv_configure_local_device_properties(const BTDriverConfig *config) {
  ble_error_t e;

  e = ble_gap_appearance_set(BLE_GAP_APPEARANCE_GENERIC_WATCH, ATT_PERM_READ);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_appearance_set: %u", e);
  }

#if 0  // FIXME: PBL-36556 -- How to configure the identity address?
  const BTDeviceAddress *addr = &config->identity_addr;
  PBL_LOG(LOG_LEVEL_DEBUG, "Setting Local Device Addr to:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)addr, sizeof(*addr));
  own_address_t own_addr = {
    .addr_type = PRIVATE_STATIC_ADDRESS
  };
  memcpy(&own_addr.addr, addr, sizeof(own_addr.addr));

  e = ble_gap_address_set(&own_addr, 0);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_address_set: %u", e);
  }
#endif

  e = ble_gap_mtu_size_set(ATT_MAX_SUPPORTED_MTU);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_mtu_size_set: %u", e);
  }

  e = ble_gap_role_set(GAP_PERIPHERAL_ROLE);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_role_set: %u", e);
  }

  // FIXME: PBL-23399: Single-source this.
  const gap_conn_params_t preferred_params = {
    .interval_min = 6,  // 7.5ms
    .interval_max = 24, // 30ms
    .slave_latency = 4,
    .sup_timeout = 600, // 6000ms
  };
  e = ble_gap_per_pref_conn_params_set(&preferred_params);
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_per_pref_conn_params_set: %u", e);
  }
}

void ble_task_assert_is_executing_on_ble_task(void) {
  PBL_ASSERTN(xTaskGetCurrentTaskHandle() == DialogTaskList[DialogTask_Ble]);
}

static void prv_configure_irk(const BTDriverConfig *config) {
  // Use the Identity Root directly as the local Identity Resolving Key:
  PBL_LOG(LOG_LEVEL_DEBUG, "Setting local IRK:");
  PBL_HEXDUMP(LOG_LEVEL_DEBUG, (const uint8_t *)&config->root_keys[SMRootKeyTypeIdentity],
              sizeof(config->root_keys[SMRootKeyTypeIdentity]));

  ble_dev_params_t *dev_params = ble_mgr_dev_params_acquire();
  memcpy(&dev_params->irk, &config->root_keys[SMRootKeyTypeIdentity], sizeof(dev_params->irk));
  ble_mgr_dev_params_release();
}

static void prv_handle_get_peer_version_complete(const ble_evt_gap_get_peer_version_t *evt) {
  Connection *conn = connection_by_idx_check(evt->conn_idx);

  BTDeviceInternal addr;
  connection_get_address(conn, &addr);

  BleRemoteVersionInfoReceivedEvent event = {
    .peer_address = addr,
    .remote_version_info = {
      .version_number = evt->version,
      .company_identifier = evt->company_identifier,
      .subversion_number = evt->subversion,
    }
  };

  hc_endpoint_gap_le_connect_send_peer_version_info(&event);
}

static void prv_ble_peripheral_task(void *params) {
  const BleTaskInitInfo *init_info = (const BleTaskInitInfo *)params;

  PBL_LOG(LOG_LEVEL_DEBUG, "At least I started");

  analytics_init();
  hc_endpoint_analytics_send_reboot_info();
  debug_reboot_reason_print();

  s_ble_perip_wdog_id = sys_watchdog_register(false);

  // FIXME:
  // srand(time(NULL));

  PBL_LOG(LOG_LEVEL_DEBUG, "hope for the best");
  connection_module_init();

  prv_configure_irk(init_info->config);

  ble_error_t e = ble_enable();
  if (e != BLE_STATUS_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_enable: %u", e);
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "go go go");
  ble_register_app();

  advert_init();
  local_addr_init();

  // Do all the things that clear the ATT table before registering the local services:
  prv_configure_local_device_properties(init_info->config);

  gatt_local_services_init(init_info->config);
  gatt_local_services_register();

  PBL_LOG(LOG_LEVEL_DEBUG, "start the loop");

#if TEST_HCI_ROM_PASSTHROUGH
  test_hci_passthrough();
#endif

  // Signal to the initing task that the BLE stack is up and running now!
  xSemaphoreGive(init_info->semph);

  for (;;) {
    BaseType_t ret;
    uint32_t notif;

    sys_watchdog_notify(s_ble_perip_wdog_id);
    sys_watchdog_suspend(s_ble_perip_wdog_id);

    ret = xTaskNotifyWait(0, (uint32_t) -1, &notif, portMAX_DELAY);
    configASSERT(ret == pdTRUE);

    sys_watchdog_resume(s_ble_perip_wdog_id);

    hc_protocol_process_receive_buffer();

    extern bool should_log_about_mic_error(uint32_t *max_subsequent_failures);
    uint32_t num_subsequent_failures = 0;
    if (should_log_about_mic_error(&num_subsequent_failures)) {
      hc_endpoint_analytics_log_mic_error_detected(num_subsequent_failures);
    }

    /* notified from BLE manager, can get event */
    if (notif & BLE_APP_NOTIFY_MASK) {
      ble_evt_hdr_t *hdr;

      hdr = ble_get_event(false);
      if (!hdr) {
        goto no_event;
      }

      if (ble_service_handle_event(hdr)) {
        goto handled;
      }

      switch (hdr->evt_code) {
          // GAP Events:
        case BLE_EVT_GAP_CONNECTED:
          handle_evt_gap_connected((ble_evt_gap_connected_t *) hdr);
          break;
        case BLE_EVT_GAP_DISCONNECTED:
          prv_handle_evt_gap_disconnected((ble_evt_gap_disconnected_t *) hdr);
          break;
        case BLE_EVT_GAP_ADV_COMPLETED:
          advert_handle_completed((ble_evt_gap_adv_completed_t *) hdr);
          break;
        case BLE_EVT_GAP_CONN_PARAM_UPDATED:
          prv_handle_conn_param_update((const ble_evt_gap_conn_param_updated_t *)hdr);
          break;
        case BLE_EVT_GAP_CONN_PARAM_UPDATE_COMPLETED:
          prv_handle_conn_param_update_request_response(
              (const ble_evt_gap_conn_param_update_completed_t *)hdr);
          break;
        case BLE_EVT_GAP_CONN_PARAM_UPDATE_REQ:
          prv_handle_conn_param_update_request((const ble_evt_gap_conn_param_update_req_t *)hdr);
          break;
        case BLE_EVT_GAP_SEC_LEVEL_CHANGED:
          prv_handle_evt_gap_sec_level_changed((ble_evt_gap_sec_level_changed_t *)hdr);
          break;
        case BLE_EVT_GAP_PAIR_REQ:
          prv_handle_pairing_request((const ble_evt_gap_pair_req_t *)hdr);
          break;
        case BLE_EVT_GAP_PAIR_COMPLETED: {
          ble_evt_gap_pair_completed_t *evt = (ble_evt_gap_pair_completed_t *)hdr;
          prv_handle_pairing_completed(evt);
          break;
        }

        case BLE_EVT_GAP_ADDRESS_RESOLVED: {
          const ble_evt_gap_address_resolved_t *evt = (const ble_evt_gap_address_resolved_t *)hdr;
          prv_handle_address_resolved(evt);
          break;
        }

        case BLE_EVT_GAP_DEV_ADDR_UPDATED: {
          const ble_evt_gap_dev_address_updated_t *evt =
              (const ble_evt_gap_dev_address_updated_t *)hdr;
          const BTDeviceAddress *addr = (const BTDeviceAddress *)&evt->address.addr;
          PBL_LOG(LOG_LEVEL_DEBUG, "Local address updated to "BT_DEVICE_ADDRESS_FMT,
                  BT_DEVICE_ADDRESS_XPLODE_PTR(addr));
          local_addr_handle_update(addr);
          break;
        }

        case BLE_EVT_GAP_GET_PEER_VERSION_COMPLETE:
          prv_handle_get_peer_version_complete((const  ble_evt_gap_get_peer_version_t *)hdr);
          break;

          // GATT Client Events:
        case BLE_EVT_GATTC_MTU_CHANGED:
          prv_handle_gatt_mtu_changed((const ble_evt_gattc_mtu_changed_t *)hdr);
          break;

        case BLE_EVT_GATTC_BROWSE_SVC:
          gatt_client_discovery_process_service((const ble_evt_gattc_browse_svc_t *)hdr);
          break;

        case BLE_EVT_GATTC_BROWSE_COMPLETED:
          gatt_client_discovery_handle_complete((const ble_evt_gattc_browse_completed_t *)hdr);
          break;

        case BLE_EVT_GATTC_READ_COMPLETED:
          gatt_wrapper_handle_read_completed((const ble_evt_gattc_read_completed_t *)hdr);
          break;

        case BLE_EVT_GATTC_WRITE_COMPLETED:
          gatt_wrapper_handle_write_completed((const ble_evt_gattc_write_completed_t *)hdr);
          break;

        case BLE_EVT_GATTC_NOTIFICATION:
          gatt_wrapper_handle_notification((const ble_evt_gattc_notification_t *)hdr);
          break;

        case BLE_EVT_GATTC_INDICATION:
          gatt_wrapper_handle_indication((const ble_evt_gattc_indication_t *)hdr);
          break;

        case IRB_BLE_STACK_MSG:
          if (((irb_ble_stack_msg_t *)hdr)->msg_type == HCI_EVT_MSG) {
            hci_rom_passthrough_handle_evt(&((irb_ble_stack_msg_t *)hdr)->msg.hci.evt);
#if TEST_HCI_ROM_PASSTHROUGH
            test_hci_passthrough();
#endif
            break;
          }
          // FALLTHROUGH

        default: // Unhandled
          ble_handle_event_default(hdr);
          PBL_LOG(LOG_LEVEL_DEBUG, "Unhandled BLE event: 0x%"PRIx16, hdr->evt_code);
          break;
      }

    handled:
      OS_FREE(hdr);

    no_event:
      // notify again if there are more events to process in queue
      if (ble_has_event()) {
        xTaskNotify(OS_GET_CURRENT_TASK(), BLE_APP_NOTIFY_MASK, eSetBits);
      }
    }
  }
}

void ble_task_init(const BTDriverConfig *config) {
  const BleTaskInitInfo init_info = {
    .semph = xSemaphoreCreateBinary(),
    .config = config,
  };
  PBL_ASSERTN(init_info.semph);

  PBL_LOG(LOG_LEVEL_INFO, "Starting BLE Task...");

  OS_TASK_CREATE("BT", prv_ble_peripheral_task, (void *)&init_info,
                 1280 /* bytes */, mainBLE_PERIPHERAL_TASK_PRIORITY,
                 DialogTaskList[DialogTask_Ble]);
  OS_ASSERT(DialogTaskList[DialogTask_Ble]);

  xSemaphoreTake(init_info.semph, portMAX_DELAY);
  vSemaphoreDelete(init_info.semph);
}
