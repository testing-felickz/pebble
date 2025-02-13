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

#include <stdbool.h>

typedef struct ble_evt_gattc_browse_svc_t ble_evt_gattc_browse_svc_t;
typedef struct ble_evt_gattc_browse_completed_t ble_evt_gattc_browse_completed_t;
typedef struct ble_evt_gattc_indication_t ble_evt_gattc_indication_t;

void gatt_client_discovery_process_service(const ble_evt_gattc_browse_svc_t *service);
void gatt_client_discovery_handle_complete(const ble_evt_gattc_browse_completed_t *complete_event);

bool gatt_client_discovery_filter_service_changed(const ble_evt_gattc_indication_t *evt);
