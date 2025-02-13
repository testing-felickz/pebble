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

#define DEVICE_INFORMATION_SERVICE_EXPECTED_ATT_STARTING_HANDLE (12)
#define HRM_SERVICE_EXPECTED_ATT_STARTING_HANDLE (35)
#define HRM_SERVICE_EXPECTED_ATT_ENDING_HANDLE (42)
#define PEBBLE_PAIRING_SERVICE_EXPECTED_ATT_STARTING_HANDLE (23)
#define PEBBLE_PPOGATT_SERVICE_EXPECTED_ATT_STARTING_HANDLE (0xE000)

typedef struct BTDriverConfig BTDriverConfig;

void gatt_local_services_init(const BTDriverConfig *config);
void gatt_local_services_register(void);
