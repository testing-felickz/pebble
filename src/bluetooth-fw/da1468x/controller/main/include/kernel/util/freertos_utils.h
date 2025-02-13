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

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "portable.h"

static inline TickType_t milliseconds_to_ticks(uint32_t milliseconds) {
  return ((uint64_t)milliseconds * configTICK_RATE_HZ) / 1000;
}

static inline uint32_t ticks_to_milliseconds(TickType_t ticks) {
  return ((uint64_t)ticks * 1000) / configTICK_RATE_HZ;
}
