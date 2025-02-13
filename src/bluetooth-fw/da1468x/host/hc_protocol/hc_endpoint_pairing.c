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

#include "hc_protocol/hc_endpoint_pairing.h"
#include "hc_protocol/hc_protocol.h"

#include "pairing_confirm_impl.h"

#include "system/logging.h"

#include <bluetooth/pairing_confirm.h>

void hc_endpoint_pairing_handler(const HcProtocolMessage *msg) {
  switch (msg->command_id) {
    case HcMessageID_Pairing_PairingRequest: {
      const HcProtocolMessagePairingRequestPayload *request =
          (const HcProtocolMessagePairingRequestPayload *)msg->payload;
      pairing_confirm_handle_request(&request->device);
      break;
    }

    case HcMessageID_Pairing_PairingComplete: {
      const HcProtocolMessagePairingCompletePayload *request =
          (const HcProtocolMessagePairingCompletePayload *)msg->payload;
      pairing_confirm_handle_complete(&request->device, request->status);
      break;
    }

    default:
      PBL_LOG(LOG_LEVEL_ERROR, "Unexpected cmd ID: %d", msg->command_id);
      break;
  }
}

void hc_endpoint_pairing_send_pairing_response(const BTDeviceInternal *device, bool is_confirmed) {
  const HcProtocolMessagePairingResponsePayload payload = {
    .device = *device,
    .is_confirmed = is_confirmed,
  };
  hc_protocol_enqueue_with_payload(HcEndpointID_Pairing, HcMessageID_Pairing_PairingResponse,
                                   (const uint8_t *)&payload, sizeof(payload));
}
