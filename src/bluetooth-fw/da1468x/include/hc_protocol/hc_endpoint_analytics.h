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

#include <bluetooth/analytics.h>
#include <bluetooth/bluetooth_types.h>
#include <bluetooth/conn_event_stats.h>

#include <util/attributes.h>
#include <util/build_id.h>

// Must keep these in sync with table in host/hc_endpoint_analytics
typedef enum DialogAnalyticsMetric {
  DialogAnalyticMetric_Test,
  DialogAnalyticMetric_Count,
} DialogAnalyticsMetric;

typedef enum {
  HcMessageID_Analytics_CollectBLEParameters = 0x01, // Host -> BT Controller
  HcMessageID_Analytics_GetConnectionQuality = 0x02, // Host -> BT Controller
  HcMessageID_Analytics_LogRebootInfo = 0x03, // BT Controller -> Host
  HcMessageID_Analytics_GetHeartbeatData = 0x04, // Host -> BT Controller
  HcMessageID_Analytics_GetConnEventStats = 0x05, // Host -> BT Controller
  HcMessageID_Analytics_LogBleMicErrorEvent = 0x06,  // BT Controller -> Host
} HcMessageID_Analytics;

typedef struct PACKED HcAnalyticsLogBleMicErrorEvent {
  uint32_t num_subsequent_mic_errors;
} HcAnalyticsLogBleMicErrorEvent;

typedef struct PACKED HcAnalyticsCollectBleParameters {
  LEChannelMap le_chan_map_res;
  bool success;
} HcAnalyticsCollectBleParameters;

typedef struct PACKED HcAnalyticsGetConnectionQuality {
  int8_t rssi;
  bool success;
} HcAnalyticsGetConnectionQuality;

typedef struct PACKED HcAnalyticsRebootInfo {
  uint8_t build_id[BUILD_ID_EXPECTED_LEN];
  uint32_t last_crash_lr;
  uint32_t reboot_reason_code;
} HcAnalyticsRebootInfo;

typedef struct SerializedAnalytic {
  uint32_t metric;
  uint32_t value;
} SerializedAnalytic;

typedef struct PACKED HcAnalyticsHeartbeatData {
  uint32_t count;
  SerializedAnalytic analytics[];
} HcAnalyticsHeartbeatData;

//! Host functions

void hc_endpoint_analytics_host_handler(const HcProtocolMessage *msg);

bool hc_endpoint_analytics_collect_ble_parameters(LEChannelMap *le_chan_map_res);

bool hc_endpoint_analytics_get_connection_quality(const BTDeviceInternal *device, int8_t *rssi_out);

void hc_endpoint_analytics_collect_heartbeat_data(void);

//! Controller Functions

void hc_endpoint_analytics_ctlr_handler(const HcProtocolMessage *msg);

//! Host -> BT Controller
bool hc_endpoint_analytics_get_conn_event_stats(SlaveConnEventStats *stats);

//! BT Controller -> Host
void hc_endpoint_analytics_send_reboot_info(void);
void hc_endpoint_analytics_log_mic_error_detected(uint32_t num_subsequent_mic_errors);
