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

#include "comm/bluetooth_analytics.h"
#include "kernel/pbl_malloc.h"
#include "services/common/analytics/analytics.h"
#include "services/common/system_task.h"
#include "system/logging.h"

#include <bluetooth/analytics.h>

#include <stdbool.h>

bool hc_endpoint_analytics_collect_ble_parameters(LEChannelMap *le_chan_map_res) {
  HcProtocolMessage *msg = hc_protocol_enqueue_with_payload_and_expect(
      HcEndpointID_Analytics, HcMessageID_Analytics_CollectBLEParameters, NULL, 0);

  if (!msg) {
    // Should be a basic log for timed out expect in hc_protocol layer
    return false;
  }

  HcAnalyticsCollectBleParameters *data = (HcAnalyticsCollectBleParameters *)&msg->payload[0];
  const bool rv = data->success;
  if (rv) {
    *le_chan_map_res = data->le_chan_map_res;
  }

  kernel_free(msg);
  return rv;
}

bool hc_endpoint_analytics_get_connection_quality(const BTDeviceInternal *device,
                                                  int8_t *rssi_out) {
  HcProtocolMessage *msg = hc_protocol_enqueue_with_payload_and_expect(
      HcEndpointID_Analytics, HcMessageID_Analytics_GetConnectionQuality,
      (uint8_t *)device, sizeof(*device));

  if (!msg) {
    // Should be a basic log for timed out expect in hc_protocol layer
    return false;
  }

  HcAnalyticsGetConnectionQuality *data = (HcAnalyticsGetConnectionQuality *)&msg->payload[0];
  const bool rv = data->success;
  if (rv) {
    *rssi_out = data->rssi;
  }

  kernel_free(msg);
  return rv;
}

static AnalyticsMetric prv_get_fw_metric(DialogAnalyticsMetric metric) {
  switch (metric) {
    // insert future DialogAnalyticsMetric's here
    case DialogAnalyticMetric_Test:
    case DialogAnalyticMetric_Count:
      return ANALYTICS_METRIC_INVALID;
    // no default so that any DialogAnalyticsMetric's that get added cause an error if they aren't
    // included in the switch statement
  }
  return metric;
}

void hc_endpoint_analytics_collect_heartbeat_data(void) {
  HcProtocolMessage *msg = hc_protocol_enqueue_with_payload_and_expect(
      HcEndpointID_Analytics, HcMessageID_Analytics_GetHeartbeatData, NULL, 0);

  if (!msg) {
    // Should be a basic log for timed out expect in hc_protocol layer
    return;
  }

  HcAnalyticsHeartbeatData *data = (HcAnalyticsHeartbeatData *)&msg->payload[0];
  for (uint32_t i = 0; i < data->count; i++) {
    SerializedAnalytic analytic = data->analytics[i];
    const AnalyticsMetric metric = prv_get_fw_metric(analytic.metric);
    if (metric != ANALYTICS_METRIC_INVALID) {
      analytics_set(metric, analytic.value, AnalyticsClient_System);
    }
  }
  kernel_free(msg);
}

bool hc_endpoint_analytics_get_conn_event_stats(SlaveConnEventStats *stats) {
  HcProtocolMessage *msg = hc_protocol_enqueue_with_payload_and_expect(
      HcEndpointID_Analytics, HcMessageID_Analytics_GetConnEventStats,
      NULL, 0);

  if (!msg) {
    // Should be a basic log for timed out expect in hc_protocol layer
    return false;
  }

  memcpy(stats, &msg->payload[0], sizeof(*stats));
  kernel_free(msg);
  return true;
}

static void prv_analytics_bt_chip_boot_cb(void *context) {
  HcAnalyticsRebootInfo *info = context;
  analytics_event_bt_chip_boot(info->build_id, info->last_crash_lr, info->reboot_reason_code);
  kernel_free(info);
}

void hc_endpoint_analytics_host_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Analytics_LogRebootInfo: {
      HcAnalyticsRebootInfo *info = (HcAnalyticsRebootInfo *) &msg->payload[0];
      HcAnalyticsRebootInfo *copy = kernel_malloc(sizeof(*info));
      if (copy) {
        memcpy(copy, info, sizeof(*info));
        // We put to system task because analytics need to be init'ed first
        system_task_add_callback(prv_analytics_bt_chip_boot_cb, copy);
      }
      break;
    }
    case HcMessageID_Analytics_LogBleMicErrorEvent: {
      HcAnalyticsLogBleMicErrorEvent *info = (HcAnalyticsLogBleMicErrorEvent *)&msg->payload[0];
      bluetooth_analytics_ble_mic_error(info->num_subsequent_mic_errors);
      break;
    }
    default:
      break;
  }
}
