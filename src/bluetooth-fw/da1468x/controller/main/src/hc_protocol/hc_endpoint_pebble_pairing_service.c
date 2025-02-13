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

#include "hc_protocol/hc_endpoint_pebble_pairing_service.h"
#include "hc_protocol/hc_protocol.h"

#include "connection.h"
#include "kernel/pbl_malloc.h"

#include <bluetooth/pebble_pairing_service.h>

#include <string.h>

void hc_endpoint_pebble_pairing_service_send_ios_app_termination_detected(void) {
  hc_protocol_enqueue_with_payload(HcEndpointID_PebblePairingService,
                                   HcMessageID_PebblePairingServiceiOSAppTerminationDetected,
                                   NULL, 0);
}

void hc_endpoint_pebble_pairing_service_found_gateway(BTDeviceInternal *device) {
  hc_protocol_enqueue_with_payload(HcEndpointID_PebblePairingService,
                                   HcMessageID_PebblePairingServiceFoundGateway,
                                   (uint8_t *)device, sizeof(*device));
}

void hc_endpoint_pebble_pairing_service_send_conn_params(const Connection *connection,
    const PebblePairingServiceConnParamsWrite *params, size_t params_length) {
  size_t len = (sizeof(HcProtocolMessage) + offsetof(HcPpsConnParamsPayload, conn_params)
                + params_length);
  HcProtocolMessage *msg = (HcProtocolMessage *)kernel_zalloc_check(len);
  msg->endpoint_id = HcEndpointID_PebblePairingService;
  msg->command_id = HcMessageID_PebblePairingServiceConnParams;
  msg->message_length = len;

  HcPpsConnParamsPayload *payload = (HcPpsConnParamsPayload *)&msg->payload[0];
  connection_get_address(connection, &payload->device);
  memcpy(&payload->conn_params, params, params_length);

  hc_protocol_enqueue(msg);
  kernel_free(msg);
}
