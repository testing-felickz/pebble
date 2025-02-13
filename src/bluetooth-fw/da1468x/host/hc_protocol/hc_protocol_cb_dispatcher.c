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

#include "hc_protocol/hc_protocol.h"

#include "kernel/pbl_malloc.h"
#include "kernel/pebble_tasks.h"
#include "kernel/util/sleep.h"
#include "system/passert.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

// Note: This thread is soley responsible for executing asynchronous callbacks for
// data received from the BT Controller (via BTTrans). We do this to prevent deadlocks
// which can result if BTTrans handles both synchronous and asynchronous callbacks.

typedef struct HcProtocolCbDispatcher {
  volatile TaskHandle_t task;
  QueueHandle_t job_queue;
} HcProtocolCbDispatcher;

static HcProtocolCbDispatcher s_dispatcher = {};

typedef struct {
  HcProtocolMessageHandler cb;
  HcProtocolMessage *msg;
} HcProtocolMessageHandlerEvent;

static void prv_terminate_dispatcher_cb(const HcProtocolMessage *unused) {
  portBASE_TYPE result;
  do {
    HcProtocolMessageHandlerEvent e;
    result = xQueueReceive(s_dispatcher.job_queue, &e, 0);
    if (result == pdTRUE) {
      kernel_free(e.msg);
    }
  } while (result == pdTRUE);

  vQueueDelete(s_dispatcher.job_queue);
  s_dispatcher.job_queue = NULL;
  s_dispatcher.task = NULL;
  pebble_task_unregister(PebbleTask_BTCallback);
}

static void prv_dispatcher_main(void *unused) {
  HcProtocolMessageHandlerEvent e;
  do {
    portBASE_TYPE result = xQueueReceive(s_dispatcher.job_queue, &e, portMAX_DELAY);
    PBL_ASSERTN(result);

    e.cb(e.msg);
    kernel_free(e.msg);
  } while (e.cb != prv_terminate_dispatcher_cb);

  vTaskDelete(NULL);
}

void hc_protocol_cb_dispatch_handler(
    const HcProtocolMessageHandler handler, HcProtocolMessage *message, bool *should_free) {
  HcProtocolMessage *msg_copy = NULL;

  bool buffer_allocated = *should_free;
  if (!buffer_allocated) {
    msg_copy = (HcProtocolMessage *)kernel_malloc_check(message->message_length);
    memcpy(msg_copy, message, message->message_length);
  } else {
    msg_copy = message;
  }

  *should_free = false;

  HcProtocolMessageHandlerEvent event = {
    .cb = handler,
    .msg = msg_copy,
  };
  xQueueSendToBack(s_dispatcher.job_queue, &event, portMAX_DELAY);
}

void hc_protocol_cb_dispatcher_init(void) {
  static const int HC_PROTOCOL_CB_DISPATCHER_MAX_JOBS_QUEUED = 15;

  s_dispatcher = (HcProtocolCbDispatcher) {
    .job_queue = xQueueCreate(
        HC_PROTOCOL_CB_DISPATCHER_MAX_JOBS_QUEUED, sizeof(HcProtocolMessageHandlerEvent)),
  };

  TaskParameters_t task_params = {
     .pvTaskCode = prv_dispatcher_main,
     .pcName = "HcDispatcher",
     .usStackDepth = 3092 / sizeof( StackType_t ), // TODO: Figure out stack size we want
     .uxPriority = (tskIDLE_PRIORITY + 3) | portPRIVILEGE_BIT,
     .puxStackBuffer = NULL, // TODO: Do we want the stack to live in a particular place?
  };

  pebble_task_create(PebbleTask_BTCallback, &task_params, (TaskHandle_t *)&s_dispatcher.task);
}

void hc_protocol_cb_dispatcher_deinit(void) {
  // Signal the dispatcher task to shutdown
  HcProtocolMessageHandlerEvent event = {
    .cb = prv_terminate_dispatcher_cb,
    .msg = NULL,
  };
  // Send the kill job to the front of the queue so its processed before any
  // other events already pended
  xQueueSendToFront(s_dispatcher.job_queue, &event, portMAX_DELAY);

  while (s_dispatcher.task) {
    // Trigger context switches & give low priority tasks a chance to run until
    // the dispatcher thread wakes and exits
    psleep(2);
  }
}
