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


#include "hc_protocol/hc_endpoint_advert.h"
#include "hc_protocol/hc_endpoint_analytics.h"
#include "hc_protocol/hc_endpoint_bonding_sync.h"
#include "hc_protocol/hc_endpoint_chip_id.h"
#include "hc_protocol/hc_endpoint_discovery.h"
#include "hc_protocol/hc_endpoint_gap_le_connect.h"
#include "hc_protocol/hc_endpoint_gap_service.h"
#include "hc_protocol/hc_endpoint_gatt.h"
#include "hc_protocol/hc_endpoint_hci.h"
#include "hc_protocol/hc_endpoint_hrm.h"
#include "hc_protocol/hc_endpoint_logging.h"
#include "hc_protocol/hc_endpoint_pairing.h"
#include "hc_protocol/hc_endpoint_pebble_pairing_service.h"
#include "hc_protocol/hc_endpoint_responsiveness.h"
#include "hc_protocol/hc_protocol.h"

#include <stddef.h>

// These are the handlers that get called when a message is received on the FW MCU side
const HcProtocolMessageHandler g_hc_protocol_endpoints_table[HcEndpointIDCount] = {
  [HcEndpointID_Invalid] = NULL,
  [HcEndpointID_Hci] = hc_endpoint_hci_handler,
  [HcEndpointID_GapService] = hc_endpoint_gap_service_resp_handler,
  [HcEndpointID_Id] = hc_endpoint_chip_id_handler,
  [HcEndpointID_Advert] = hc_endpoint_advert_resp_handler,
  [HcEndpointID_Responsiveness] = hc_endpoint_responsiveness_handler,
  [HcEndpointID_GapLEConnect] = hc_endpoint_gap_le_connect_handler,
  [HcEndpointID_Gatt] = hc_endpoint_gatt_handler_host,
  [HcEndpointID_Discovery] = hc_endpoint_discovery_handler,
  [HcEndpointID_BondingSync] = hc_endpoint_bonding_sync_handler,
  [HcEndpointID_PebblePairingService] = hc_endpoint_pebble_pairing_service_handler,
  [HcEndpointID_Pairing] = hc_endpoint_pairing_handler,
  [HcEndpointID_Logging] = hc_endpoint_logging_handler,
  [HcEndpointID_Analytics] = hc_endpoint_analytics_host_handler,
  [HcEndpointID_HRM] = hc_endpoint_hrm_handler,
};
