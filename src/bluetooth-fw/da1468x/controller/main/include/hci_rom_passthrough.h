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

#include "ad_ble.h"

#include <inttypes.h>
#include <stdbool.h>

//! Injects an HCI command into Dialog's ROM even if the chip is not configured
//! with BLE_STACK_PASSTHROUGH_MODE & BLE_EXTERNAL_HOST. This should be used
//! conservatively as it effectively bypasses the dialog SDK stack. It does however
//! allow one to add support for things not yet exported as an SDK API (i.e BLE
//! Direct Test Modes)
bool hci_rom_passthrough_send_cmd(
    uint16_t ogf, uint16_t ocf, const uint8_t *param_buf, uint8_t param_length);

//! Commands issued with hci_rom_passthrough_send_cmd() will bubble up
//! HCI event(s). This callback is invoked from the app task to handle these events
void hci_rom_passthrough_handle_evt(hci_evt_msg_t *msg);
