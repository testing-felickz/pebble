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

#include "hc_protocol/hc_endpoint_hci.h"
#include "hc_protocol/hc_protocol.h"
#include "system/hexdump.h"
#include "system/logging.h"
#include "system/passert.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

// This routine should only ever be called by the passthrough event handler. It
// returns true if the msg was consumed
extern bool bt_test_hci_event_handled(const uint8_t *hci_msg, uint16_t payload_len);

void hc_endpoint_hci_handler(const HcProtocolMessage *msg) {
  if (msg->command_id != HcMessageID_Hci_Evt) {
    PBL_LOG(LOG_LEVEL_WARNING, "Unsupported HCI message: 0x%x", (int)msg->command_id);
    return;
  }

  int payload_len = msg->message_length - sizeof(*msg);
  PBL_ASSERTN(payload_len > 0);

  //  PBL_HEXDUMP(LOG_LEVEL_DEBUG, &msg->payload[0], payload_len);

  const char *payload = (const char *)&msg->payload[0];

  if (bt_test_hci_event_handled((const uint8_t *)payload, payload_len)) {
    // We are done, don't dump to response dbgserial
    return;
  }

  for (int i = 0; i < payload_len; i++) {
    // TODO: Add support for accessory port
    dbgserial_putchar_lazy(payload[i]);
  }
}

void hc_endpoint_enqueue_hci_cmd(const uint8_t *hci_buf, uint8_t payload_len) {
  // skip the HCI_CMD byte since that's already encoded in the message
  hci_buf++;
  payload_len -= 1;

  hc_protocol_enqueue_with_payload(HcEndpointID_Hci, HcMessageID_Hci_Cmd,
                                   hci_buf, payload_len);
}
