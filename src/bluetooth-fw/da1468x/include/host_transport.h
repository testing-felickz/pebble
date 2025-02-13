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

//! @file host_transport.h Common interfaces (shared between host and controller side FWs)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum HostTransportEnqueueStatus {
  HostTransportEnqueueStatus_Success,
  // The stack is not initialized yet or has crashed and is about to be reset
  HostTransportEnqueueStatus_Failure,
  // Host Transport buffers are full. In a few they should be drained so retry then
  HostTransportEnqueueStatus_RetryLater
} HostTransportEnqueueStatus;

HostTransportEnqueueStatus host_transport_tx_enqueue(const uint8_t *data, size_t length);

size_t host_transport_rx_get_length(void);

//! @return True if the caller must kernel_free(*data_ptr_out) at some point.
bool host_transport_rx_read(uint8_t **data_ptr_out, size_t length);

void host_transport_rx_consume(size_t length);

bool host_transport_is_current_task_host_transport_task(void);
