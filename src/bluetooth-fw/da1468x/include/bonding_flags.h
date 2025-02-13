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

//! Flags for work-arounds in the BT driver that the upper FW does not need to know about,
//! but that do need to be persisted by bt_persistent_storage_...
typedef enum {
  BleBondingFlag_ShouldAutoAcceptRePairing = (1 << 0),
  BleBondingFlag_IsReversedPPoGATTEnabled = (1 << 1),
  // NOTE: bt_persistent_storage_... uses only 5 bits to store this
} BleBondingFlag;
