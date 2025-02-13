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

#include <util/list.h>

#include <stdint.h>
#include <stdbool.h>

typedef enum GattOpType {
  GattOpType_Read,
  GattOpType_Write
} GattOpType;

typedef enum GattRespDest {
  GattRespDestNone,
  GattRespDestHost,
  GattRespDestController,
} GattRespDest;

typedef struct GattOperation {
  ListNode node;
  uintptr_t object_ref;
  GattRespDest resp_dest;
  GattOpType op_type;
} GattOperation;
