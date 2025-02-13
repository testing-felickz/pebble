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

#include "core_dump.h"
#include "hc_protocol/hc_protocol.h"
#include "host_transport.h"

#include "kernel/pbl_malloc.h"
#include "os/mutex.h"
#include "os/tick.h"
#include "system/logging.h"
#include "system/passert.h"

#include "util/list.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <inttypes.h>
#include <string.h>

typedef enum HcProtocolState {
  HcProtoclStateNotInitialized = 0,
  HcProtoclStateIsInitialized,
  HcProtoclStateDeinitializing,
} HcProtocolState;

#define TIMEOUT_TICKS (configTICK_RATE_HZ / 2)
#define NUM_CONSEC_FAILURES 5

extern const HcProtocolMessageHandler g_hc_protocol_endpoints_table[HcEndpointIDCount];

typedef struct {
  ListNode node;
  SemaphoreHandle_t semaphore;
  const HcProtocolMessage *request;
  HcProtocolMessage *response;
} HcExpectation;

static PebbleRecursiveMutex *s_hc_lock;
static HcProtocolState s_hc_state;
static HcExpectation *s_expectations_head;
static uint8_t s_hc_next_transaction_id;
static SemaphoreHandle_t s_retry_semph;

static volatile uint8_t s_outstanding_enqueues;

static uint8_t s_num_consec_enqueue_failures;
#if !BT_CONTROLLER_BUILD
static uint8_t s_num_consec_request_failures;
#endif

static void prv_lock(void) {
  mutex_lock_recursive(s_hc_lock);
}

static void prv_unlock(void) {
  mutex_unlock_recursive(s_hc_lock);
}

static void prv_update_hc_state(HcProtocolState state) {
  prv_lock();
  s_hc_state = state;
  prv_unlock();
}

//! WARNING: Will log if function is called and state is not initialized.
static bool prv_check_initialized(void) {
  bool is_initialized;
  HcProtocolState state;
  prv_lock();
  {
    state = s_hc_state;
    is_initialized = (state == HcProtoclStateIsInitialized);
  }
  prv_unlock();
  if (!is_initialized) {
    PBL_LOG(LOG_LEVEL_WARNING, "prv_check_initialized called when hc_protocol is not initialized, "
            "state: %d", state);
  }
  return is_initialized;
}

static TickType_t prv_remaining_ticks(TickType_t timeout_ticks, TickType_t start_ticks) {
  const TickType_t now = xTaskGetTickCount();
  const TickType_t elapsed = (now - start_ticks);
  if (timeout_ticks > elapsed) {
    return timeout_ticks - elapsed;
  }
  return 0;
}

static bool prv_still_processing_enqueues(void) {
  bool done;
  prv_lock();
  {
    done = (s_outstanding_enqueues != 0);
  }
  prv_unlock();
  return done;
}

// Only to be called by prv_enqueue(). This function keeps track of how many
// enqueues are outstanding. If an enqueue is about to start but the stack is
// no longer up, it does NOT bump the job count as we expect prv_enqueue() to
// bail immediately
static bool prv_update_enqueue_count(bool start) {
  bool initialized;
  prv_lock();
  {
    initialized = prv_check_initialized();
    if (initialized || !start) {
      s_outstanding_enqueues += (start) ? 1 : -1;
    }
  }
  prv_unlock();
  return initialized;
}

static bool prv_enqueue(HcProtocolMessage *message) {
  PBL_ASSERTN(message->message_length >= sizeof(HcProtocolMessage));

  if (!prv_update_enqueue_count(true)) {
    return false;
  }

  bool success = true;

  // Retry for 500ms
  TickType_t start_ticks = xTaskGetTickCount();

  while (1) {
    HostTransportEnqueueStatus status =
         host_transport_tx_enqueue((const uint8_t *)message, message->message_length);

    if (status != HostTransportEnqueueStatus_RetryLater) {
      success = (status == HostTransportEnqueueStatus_Success);
      goto done;
    }

    TickType_t remaining_ticks = prv_remaining_ticks(TIMEOUT_TICKS, start_ticks);

    bool is_timeout;
    if (remaining_ticks == 0) {
      is_timeout = true;
    } else {
      is_timeout = (xSemaphoreTake(s_retry_semph, remaining_ticks) == pdFALSE);
    }

    bool initialized = prv_check_initialized();
    if (is_timeout || !initialized) {
      PBL_LOG(LOG_LEVEL_ERROR, "Failed to enqueue HC request (endpoint:%d, command:%"PRIu8")",
              message->endpoint_id, message->command_id);
      s_num_consec_enqueue_failures++;
      if (initialized && (s_num_consec_enqueue_failures >= NUM_CONSEC_FAILURES)) {
        core_dump_and_reset_or_reboot();
      }
      success = false;
      goto done;
    }
  }
  s_num_consec_enqueue_failures = 0;

done:
  prv_update_enqueue_count(false /* stopping */);
  return success;
}

static void prv_assign_next_transaction_id(HcProtocolMessage *message) {
  prv_lock();
  message->transaction_id.sn = s_hc_next_transaction_id;
  const uint8_t max_sn = ((1 << HC_PROTOCOL_SN_BIT_WIDTH) - 1);
  ++s_hc_next_transaction_id;
  s_hc_next_transaction_id %= max_sn;
  prv_unlock();

  message->transaction_id.is_response = false;
}

bool hc_protocol_enqueue(HcProtocolMessage *message) {
  if (!prv_check_initialized()) {
    return false;
  }

  prv_assign_next_transaction_id(message);
  return prv_enqueue(message);
}

static HcProtocolMessage *prv_create_message(HcEndpointID endpoint_id, HcCommandID command_id,
                                             const uint8_t *request_payload,
                                             size_t request_payload_length) {
  size_t message_length = sizeof(HcProtocolMessage) + request_payload_length;
  HcProtocolMessage *message = (HcProtocolMessage *)kernel_zalloc_check(message_length);
  message->message_length = message_length;
  message->endpoint_id = endpoint_id;
  message->command_id = command_id;
  if (request_payload_length) {
    memcpy(message->payload, request_payload, request_payload_length);
  }
  return message;
}

bool hc_protocol_enqueue_with_payload(HcEndpointID endpoint_id, HcCommandID command_id,
                                      const uint8_t *request_payload,
                                      size_t request_payload_length) {
  if (!prv_check_initialized()) {
    return false;
  }

  HcProtocolMessage *message = prv_create_message(endpoint_id, command_id, request_payload,
                                                  request_payload_length);
  const bool rv = hc_protocol_enqueue(message);
  kernel_free(message);
  return rv;
}

#if !BT_CONTROLLER_BUILD
static HcExpectation *prv_set_expectation_for_request(const HcProtocolMessage *request_message) {
  SemaphoreHandle_t semaphore = xSemaphoreCreateBinary();
  PBL_ASSERTN(semaphore);

  HcExpectation *expectation = kernel_malloc_check(sizeof(HcExpectation));
  *expectation = (HcExpectation) {
    .semaphore = semaphore,
    .request = request_message,
    .response = NULL,
  };
  prv_lock();
  s_expectations_head = (HcExpectation *) list_prepend((ListNode *)s_expectations_head,
                                                       &expectation->node);
  prv_unlock();
  return expectation;
}

static void prv_cleanup_expectation(HcExpectation *expectation) {
  prv_lock();
  list_remove((ListNode *)expectation, (ListNode **)&s_expectations_head, NULL);
  prv_unlock();
  vSemaphoreDelete(expectation->semaphore);
  kernel_free(expectation);
}

static HcProtocolMessage *prv_expect(HcExpectation *expectation) {
  HcProtocolMessage *response = NULL;
  // Save for debugging
  const HcCommandID cmd_id = expectation->request->command_id;
  const HcEndpointID end_id = expectation->request->endpoint_id;

  TickType_t time_ticks = milliseconds_to_ticks(HC_PROTOCOL_DEFAULT_RESPONSE_TIMEOUT_MS);
  if (end_id == HcEndpointID_Ctl) {
    time_ticks = milliseconds_to_ticks(HC_PROTOCOL_DEFAULT_CTL_ENDPOINT_RESPONSE_TIMEOUT_MS);
  }

  xSemaphoreTake(expectation->semaphore, time_ticks);
  prv_lock();
  {
    // If we've timed out, expectation->response is NULL:
    response = expectation->response;
    // Set request to NULL, so it can't be matched any longer (important in timeout scenario)
    expectation->request = NULL;
  }
  prv_unlock();

  if (response) {
    s_num_consec_request_failures = 0;
  } else {
    s_num_consec_request_failures++;
    PBL_LOG(LOG_LEVEL_ERROR, "HC request timed out (endpoint:%d, command:%"PRIu8")",
            end_id, cmd_id);
    if (prv_check_initialized() && (s_num_consec_request_failures >= NUM_CONSEC_FAILURES)) {
      core_dump_and_reset_or_reboot();
    }
  }

  return response;
}

HcProtocolMessage *hc_protocol_enqueue_with_payload_and_expect(HcEndpointID endpoint_id,
                                                               HcCommandID command_id,
                                                               const uint8_t *request_payload,
                                                               size_t request_payload_length) {
  if (!prv_check_initialized()) {
    return false;
  }

  HcProtocolMessage *request = prv_create_message(endpoint_id, command_id, request_payload,
                                                  request_payload_length);
  // `response` will be NULL if it failed.
  HcProtocolMessage *response = hc_protocol_enqueue_and_expect(request);
  kernel_free(request);
  return response;
}

HcProtocolMessage *hc_protocol_enqueue_and_expect(HcProtocolMessage *request_message) {
  if (!prv_check_initialized()) {
    return false;
  }

  // Don't allow because we'd deadlock otherwise:
  PBL_ASSERTN(!host_transport_is_current_task_host_transport_task());

  prv_assign_next_transaction_id(request_message);
  HcExpectation *expectation = prv_set_expectation_for_request(request_message);

  if (!prv_enqueue(request_message)) {
    prv_cleanup_expectation(expectation);
    return NULL;
  }

  HcProtocolMessage *response_message = prv_expect(expectation);
  prv_cleanup_expectation(expectation);

  return response_message;
}
#endif

bool hc_protocol_enqueue_response(const HcProtocolMessage *to_request,
                                  const uint8_t *response_payload, size_t response_payload_length) {
  if (!prv_check_initialized()) {
    return false;
  }

  PBL_ASSERTN(!to_request->transaction_id.is_response);
  size_t message_length = sizeof(HcProtocolMessage) + response_payload_length;
  HcProtocolMessage *response = kernel_malloc_check(message_length);
  response->message_length = message_length;
  response->endpoint_id = to_request->endpoint_id;
  response->command_id = to_request->command_id;
  response->transaction_id.is_response = true;
  response->transaction_id.sn = to_request->transaction_id.sn;
  if (response_payload_length) {
    memcpy(response->payload, response_payload, response_payload_length);
  }
  bool rv = prv_enqueue(response);
  kernel_free(response);
  return rv;
}

static HcProtocolMessageHandler prv_get_handler_for_endpoint_id(HcEndpointID endpoint_id) {
  if (endpoint_id >= HcEndpointIDCount) {
    return NULL;
  }
  return g_hc_protocol_endpoints_table[endpoint_id];
}

static void prv_dispatch_message_to_static_handler(HcProtocolMessage *message, bool *should_free) {
  const HcProtocolMessageHandler handler = prv_get_handler_for_endpoint_id(message->endpoint_id);
  if (!handler) {
    PBL_LOG(LOG_LEVEL_ERROR, "No handler for endpoint ID %u", message->endpoint_id);
    return;
  }

  hc_protocol_cb_dispatch_handler(handler, message, should_free);
}

static bool prv_expectation_for_message_list_filter_cb(ListNode *found_node, void *data) {
  HcExpectation *expectation = (HcExpectation *)found_node;
  const HcProtocolMessage *message = (const HcProtocolMessage *)data;
  if (!expectation->request) {
    // Already being handled or timed out.
    return false;
  }
  if (!message->transaction_id.is_response) {
    return false;
  }
  return (message->transaction_id.sn == expectation->request->transaction_id.sn);
}

static HcExpectation *prv_expectation_for_message(HcProtocolMessage *message) {
  return (HcExpectation *)list_find((ListNode *)s_expectations_head,
                                    prv_expectation_for_message_list_filter_cb, message);
}

static bool prv_try_handle_expectation(HcProtocolMessage *message, bool *should_free) {
  prv_lock();
  HcExpectation *expectation = prv_expectation_for_message(message);
  if (!expectation) {
    prv_unlock();
    return false;
  }

  // Make a heap copy if needed, or transfer ownership if the message is already heap-allocated:
  HcProtocolMessage *response_heap_copy;
  if (*should_free) {
    *should_free = false;
    response_heap_copy = message;
  } else {
    response_heap_copy = (HcProtocolMessage *) kernel_malloc_check(message->message_length);
    memcpy(response_heap_copy, message, message->message_length);
  }

  expectation->response = response_heap_copy;
  xSemaphoreGive(expectation->semaphore);
  prv_unlock();
  return true;
}

static HcProtocolMessage *prv_get_message(size_t length, bool *should_free) {
  uint8_t *rx_data = NULL;
  *should_free = host_transport_rx_read(&rx_data, length);
  return (HcProtocolMessage *) rx_data;
}

void hc_protocol_process_receive_buffer(void) {
  if (!prv_check_initialized()) {
    return;
  }

  size_t rx_length = host_transport_rx_get_length();
  while (rx_length) {
    if (rx_length < sizeof(HcProtocolMessage)) {
      // Header not received completely yet
      return;
    }

    bool should_free = false;
    HcProtocolMessage *header = prv_get_message(sizeof(HcProtocolMessage), &should_free);
    const size_t message_length = header->message_length;
    if (should_free) {
      kernel_free(header);
    }

    if (rx_length < message_length) {
      // Message not received completely yet
      return;
    }

    HcProtocolMessage *message = prv_get_message(message_length, &should_free);
    if (!prv_try_handle_expectation(message, &should_free)) {
      prv_dispatch_message_to_static_handler(message, &should_free);
    }

    host_transport_rx_consume(message_length);
    if (should_free) {
      kernel_free(message);
    }

    rx_length -= message_length;
  }
}

void hc_protocol_buffer_gained_space(void) {
  PBL_ASSERTN(s_retry_semph);
  xSemaphoreGive(s_retry_semph);
}

//! Should be called only once at boot.
void hc_protocol_boot(void) {
  if (s_hc_lock) {
    return;
  }
  s_hc_lock = mutex_create_recursive();
}

//! Should be called when stack is brought up.
void hc_protocol_init(void) {
  s_outstanding_enqueues = 0;
  s_hc_next_transaction_id = 0;
  s_retry_semph = xSemaphoreCreateBinary();
  prv_update_hc_state(HcProtoclStateIsInitialized);
}

//! Should be called when stack is torn down.
void hc_protocol_deinit(void) {
  prv_update_hc_state(HcProtoclStateDeinitializing);

  while (prv_still_processing_enqueues()) {
    // Give the semaphore in case a task is waiting on it
    xSemaphoreGive(s_retry_semph);
    // Give the task some time to process it
    vTaskDelay(2);
  }

  // At this point it should no longer be possible for someone to use the retry
  // semaphore so delete it
  SemaphoreHandle_t s_retry_semph_tmp = s_retry_semph;
  s_retry_semph = NULL;
  vSemaphoreDelete(s_retry_semph_tmp);

  prv_lock();
  HcExpectation *expectation = s_expectations_head;
  while (expectation) {
    if (!expectation->request) {
      // Semaphore already given or timed out, so skip it.
      continue;
    }
    // Just give, the expectation + semaphore should get cleaned up automatically now.
    xSemaphoreGive(expectation->semaphore);
    expectation = (HcExpectation *) list_get_next(&expectation->node);
  }
  prv_unlock();
}
