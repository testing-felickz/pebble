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

#include "comm/bt_conn_mgr.h"

void conn_mgr_set_bt_classic_conn_response_time_ext(
  struct Remote *remote, BtConsumer consumer, ResponseTimeState state,
  uint16_t max_period_secs, ResponsivenessGrantedHandler granted_handler) {
}

//! Same as conn_mgr_set_bt_classic_conn_response_time_ext, but without granted_handler.s
void conn_mgr_set_bt_classic_conn_response_time(
  struct Remote *remote, BtConsumer consumer, ResponseTimeState state,
  uint16_t max_period_secs) {
}
