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

#include "util/attributes.h"
#include "util/circular_buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  // - Please don't shuffle values to keep debugging simple.
  // - Please use consequtive values to avoid "gaps" in the table.
  HcEndpointID_Invalid = 0x00,
  HcEndpointID_Ctl = 0x01,
  HcEndpointID_Hci = 0x02,
  HcEndpointID_GapService = 0x03,
  HcEndpointID_Id = 0x04,
  HcEndpointID_Advert = 0x05,
  HcEndpointID_Responsiveness = 0x06,
  HcEndpointID_GapLEConnect = 0x07,
  HcEndpointID_Gatt = 0x08,
  HcEndpointID_Discovery = 0x09,
  HcEndpointID_BondingSync = 0x0a,
  HcEndpointID_PebblePairingService = 0x0b,
  HcEndpointID_Pairing = 0x0c,
  HcEndpointID_Analytics = 0x0d,
  HcEndpointID_Logging = 0x0e,
  HcEndpointID_Test = 0x0f,
  HcEndpointID_HRM = 0x10,
  HcEndpointIDCount,
} HcEndpointID;

typedef uint8_t HcCommandID;

#define HC_PROTOCOL_SN_BIT_WIDTH    (7)

typedef struct PACKED HcProtocolMessage {
  uint16_t message_length;  //!< Total message length including header, little-endian
  HcEndpointID endpoint_id:8;
  HcCommandID command_id:8;
  struct PACKED {
    uint8_t sn:HC_PROTOCOL_SN_BIT_WIDTH;
    bool is_response:1; //!< If the MSBit of the transaction_id byte is set, it's a response.
  } transaction_id; //!< Automatically assigned by hc_protocol_enqueue...()
  uint8_t rsvd[3]; //! keep the payload 4 byte aligned
  uint8_t payload[];
} HcProtocolMessage;

_Static_assert((sizeof(HcProtocolMessage) % 4) == 0, "HcProtocolMessage not 4-byte multiple");

#define HC_PROTOCOL_DEFAULT_RESPONSE_TIMEOUT_MS (1000)
// We need the control endpoint requests to succeed for BT to be in a good state so give them a
// little more time than a typical endpoint
#define HC_PROTOCOL_DEFAULT_CTL_ENDPOINT_RESPONSE_TIMEOUT_MS    \
  (HC_PROTOCOL_DEFAULT_RESPONSE_TIMEOUT_MS + 1000)

typedef void (*HcProtocolMessageHandler)(const HcProtocolMessage *msg);

//! Enqueues a message with the current host transport.
//! @param message The message to enqueue.
//! @return True if the message was successfully enqueued.
//! @note This function will automatically assign the `transaction_id`.
bool hc_protocol_enqueue(HcProtocolMessage *message);

//! Enqueues a message with the current host transport by creating a message and copying the
//! specified payload into the message.
//! @return True if the message was successfully enqueued.
//! @note This heap-allocates a buffer (and frees it before returning). If you need to send a large
//! payload and want to avoid this additional allocate-and-copy, consider using hc_protocol_enqueue.
//! @note This function will automatically assign the `transaction_id`.
bool hc_protocol_enqueue_with_payload(HcEndpointID endpoint_id, HcCommandID command_id,
                                      const uint8_t *request_payload,
                                      size_t request_payload_length);

#if !BT_CONTROLLER_BUILD // Not currently supported for Controller->Host messaging
//! Enqueues a message with the current host transport and blocks until a response message has been
//! received, that matches the `transaction_id` of the request message. If a message was not
//! received within the protocol timeout, NULL will be returned.
//! @param request_message The message with the request.
//! @return Heap-allocated message or NULL if the timeout was hit. The caller of this function is
//! responsible for calling kernel_free() to avoid leaking the memory.
//! @note This function will automatically assign the `transaction_id`.
HcProtocolMessage *hc_protocol_enqueue_and_expect(HcProtocolMessage *request_message);

//! Same as hc_protocol_enqueue_and_expect, but creates a request message and copies the specified
//! payload into the request message.
//! @note This function will automatically assign the `transaction_id`.
HcProtocolMessage *hc_protocol_enqueue_with_payload_and_expect(HcEndpointID endpoint_id,
                                                               HcCommandID command_id,
                                                               const uint8_t *request_payload,
                                                               size_t request_payload_length);
#endif

//! Enqueues a response to previously received request.
//! @param to_request The request message being responded to.
//! @param response_payload The response payload
//! @param payload_length The length of response_payload in bytes
//! @return True if the message was successfully enqueued.
//! @note This function will automatically assign the correct `transaction_id` to match with
//! to_request.
bool hc_protocol_enqueue_response(const HcProtocolMessage *to_request,
                                  const uint8_t *response_payload, size_t response_payload_length);

//! Called by the host transport every time new data got written into the receive buffer.
//! This function will go through the data in the receive buffer as returned by
//! host_transport_rx_read() and call the endpoint handler for each message. After each message is
//! handled, this function will call host_transport_rx_consume() for the handled length. The message
//! will also be free'd if needed by this function. If a message has not been received completely,
//! the endpoint handler will not yet be called.
void hc_protocol_process_receive_buffer(void);

//! Called when the buffer has added extra free space in its buffer so that the client
//! can try to enqueue data if it had previously failed.
void hc_protocol_buffer_gained_space(void);

//! Must be implemented by user of HC Trans. Called every time an asynchronous message
//! is received from the other side.
//! @param handler The callback to invoke
//! @param message The message to pass to the invoked callback
//! @param[in/out] should_free For input, indicates whether the buffer has been allocated.
//!                            As output, set to False to prevent the callee from freeing message
extern void hc_protocol_cb_dispatch_handler(
    const HcProtocolMessageHandler handler, HcProtocolMessage *message, bool *should_free);

//! Should be called only once at boot.
void hc_protocol_boot(void);

//! Should be called when stack is brought up.
void hc_protocol_init(void);

//! Should be called when stack is torn down.
void hc_protocol_deinit(void);
