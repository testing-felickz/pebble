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

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/pebble_pairing_service.h>
#include <util/attributes.h>

typedef struct Connection Connection;

typedef enum {
  HcMessageID_PebblePairingServiceiOSAppTerminationDetected = 0x01,
  HcMessageID_PebblePairingServiceFoundGateway = 0x02,
  HcMessageID_PebblePairingServiceConnParams = 0x03,
} HcMessageID_PebblePairingService;

typedef struct PACKED {
  BTDeviceInternal device;
  PebblePairingServiceConnParamsWrite conn_params;
} HcPpsConnParamsPayload;

void hc_endpoint_pebble_pairing_service_handler(const HcProtocolMessage *msg);

//! Host => Controller
void hc_endpoint_pebble_pairing_service_send_ios_app_termination_detected(void);

void hc_endpoint_pebble_pairing_service_found_gateway(BTDeviceInternal *device);

void hc_endpoint_pebble_pairing_service_send_conn_params(const Connection *connection,
    const PebblePairingServiceConnParamsWrite *params, size_t params_length);
