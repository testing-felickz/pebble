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

#include <stdint.h>
#include <stdbool.h>

typedef struct Connection Connection;
typedef struct HcProtocolMessage HcProtocolMessage;
typedef struct PPoGATTWorkAroundState PPoGATTWorkAroundState;

void ppogatt_service_init(void);
void ppogatt_service_register(uint16_t start_hdl);

bool ppogatt_emulated_server_handle_msg(uint16_t conn_idx, Connection *conn,
                                        const HcProtocolMessage *msg);
void ppogatt_inject_emulated_ppogatt_service_if_needed(uint16_t conn_idx);

void ppogatt_enable_emulated_server_wa(void);
void ppogatt_emulated_notify_phone_ppogatt_server_found(Connection *conn);
