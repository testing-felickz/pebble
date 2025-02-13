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

#include "hc_protocol/hc_endpoint_analytics.h"
#include "host_transport_protocol.h"

#include "connection.h"
#include "dialog_analytics/analytics.h"
#include "host_transport.h"
#include "kernel/pbl_malloc.h"
#include "reboot_reason.h"
#include "system/hexdump.h"
#include "system/logging.h"

// Dialog SDK:
#include "ble_common.h"
#include "ble_gap.h"

#include <bluetooth/analytics.h>
#include <bluetooth/bluetooth_types.h>

#include <inttypes.h>
#include <stdint.h>

void slave_window_stats_collect(SlaveConnEventStats *stats);

static void prv_handle_collect_ble_parameters(const HcProtocolMessage *msg) {
  uint64_t channel_map_uint;

  const ble_error_t e = ble_gap_channel_map_get(&channel_map_uint);
  const bool success = (e == BLE_STATUS_OK);

  LEChannelMap channel_map = {};
  if (success) {
    memcpy(&channel_map, &channel_map_uint, sizeof(channel_map));
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_channel_map_get err: %x", e);
  }

  HcAnalyticsCollectBleParameters data = {
    .success = success,
    .le_chan_map_res = channel_map,
  };

  hc_protocol_enqueue_response(msg, (uint8_t *)&data, sizeof(data));
}

static void prv_handle_get_connection_quality(const HcProtocolMessage *msg) {
  HcAnalyticsGetConnectionQuality data = {};

  const BTDeviceInternal *device = (BTDeviceInternal *)&msg->payload[0];
  Connection *conn = connection_by_address(device);
  if (!conn) {
    data.success = false;
    goto done;
  }

  const uint16_t conn_idx = connection_get_idx(conn);
  int8_t conn_rssi;
  const ble_error_t e = ble_gap_conn_rssi_get(conn_idx, &conn_rssi);
  const bool success = (e == BLE_STATUS_OK);
  if (!success) {
    PBL_LOG(LOG_LEVEL_ERROR, "ble_gap_conn_rssi_get err: %x", e);
    conn_rssi = 0;
  }

  data = (HcAnalyticsGetConnectionQuality) {
    .success = success,
    .rssi = conn_rssi,
  };

done:
  hc_protocol_enqueue_response(msg, (uint8_t *)&data, sizeof(data));
}

#if 0 // FIXME with PBL-38365
static void prv_analytic_each_cb(DialogAnalyticsMetric metric, uint32_t value, void *context) {
  HcAnalyticsHeartbeatData *data = context;
  data->analytics[metric].metric = cmetric;
  data->analytics[metric].value = value;
}

#define HEARTBEAT_SIZE (sizeof(HcAnalyticsHeartbeatData) + \
                        (DialogAnalyticMetric_Count * sizeof(SerializedAnalytic)))

_Static_assert(HEARTBEAT_SIZE < HOST_TRANSPORT_CTLR_TX_BUFFER_SIZE &&
               HEARTBEAT_SIZE < HOST_TRANSPORT_HOST_RX_BUFFER_SIZE,
               "Heartbeat Size must be less than HOST_TRANSPORT_TX_BUFFER_SIZE");

static void prv_handle_get_heartbeat_data(const HcProtocolMessage *msg) {
  uint32_t data_size = HEARTBEAT_SIZE;
  HcAnalyticsHeartbeatData *data = kernel_zalloc(data_size);

  if (!data) {
    PBL_LOG(LOG_LEVEL_ALWAYS, "Not enough resources to allocate analytics heartbeat");
    data_size = 0;
    goto done;
  }

  data->count = DialogAnalyticMetric_Count;
  analytics_each(prv_analytic_each_cb, data);

  analytics_reset_nodes();

done:
  hc_protocol_enqueue_response(msg, (uint8_t *)data, data_size);
  kernel_free(data);
}
#endif

static void prv_handle_get_connection_event_stats(const HcProtocolMessage *msg) {
  SlaveConnEventStats stats;
  slave_window_stats_collect(&stats);
  hc_protocol_enqueue_response(msg, (uint8_t *)&stats, sizeof(stats));
}

//! This symbol and its contents are provided by the linker script, see the
//! .note.gnu.build-id section in src/fw/stm32f2xx_flash_fw.ld
extern const ElfExternalNote DIALOG_BUILD_ID;

void hc_endpoint_analytics_send_reboot_info(void) {
  const RebootReasonCode reason = reboot_reason_get_last_reboot_reason();
  if (reason == RebootReasonCode_Shutdown || reason == RebootReasonCode_Unknown) {
    // Don't send the reboot reason to analytics for insignificant data.
    return;
  }

  HcAnalyticsRebootInfo info = {
    .last_crash_lr = reboot_reason_get_crash_lr(),
    .reboot_reason_code = reason,
  };
  memcpy(info.build_id, &DIALOG_BUILD_ID.data[DIALOG_BUILD_ID.name_length], BUILD_ID_EXPECTED_LEN);

  hc_protocol_enqueue_with_payload(HcEndpointID_Analytics, HcMessageID_Analytics_LogRebootInfo,
                                   (uint8_t *)&info, sizeof(info));
}

void hc_endpoint_analytics_log_mic_error_detected(uint32_t num_subsequent_mic_errors) {
  HcAnalyticsLogBleMicErrorEvent info = {
    .num_subsequent_mic_errors = num_subsequent_mic_errors,
  };

  hc_protocol_enqueue_with_payload(
      HcEndpointID_Analytics, HcMessageID_Analytics_LogBleMicErrorEvent, (uint8_t *)&info,
      sizeof(info));
}

void hc_endpoint_analytics_ctlr_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Analytics_CollectBLEParameters:
      prv_handle_collect_ble_parameters(msg);
      break;
    case HcMessageID_Analytics_GetConnectionQuality:
      prv_handle_get_connection_quality(msg);
      break;
#if 0 // Enable with PBL-38365
    case HcMessageID_Analytics_GetHeartbeatData:
      prv_handle_get_heartbeat_data(msg);
      break;
#endif
    case HcMessageID_Analytics_GetConnEventStats:
      prv_handle_get_connection_event_stats(msg);
      break;
    default:
      PBL_LOG(LOG_LEVEL_ERROR, "HcAnalytics: unhandled message id: %d", msg->command_id);
  }
}
