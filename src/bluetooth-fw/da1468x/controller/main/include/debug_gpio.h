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

//! This is a debug utlility that makes it easy to toggle gpios to debug paths which are very
//! sensitive to timing.
//! To use it simply:
//!   1) Call debug_gpio_init() before you want to track things
//!   2) Call debug_gpio_toggle() to flip the gpio state

//! Initializs DEBUG_GPIOs defined in board config and drives them all low
void debug_gpio_init(void);

//! Toggles the debug_gpio from it's current state
void debug_gpio_toggle(int debug_gpio_num);

//! Drives the state of the specified debug gpio
void debug_gpio_set_active(int debug_gpio, bool is_active);
