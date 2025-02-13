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

#include <util/attributes.h>

#include "debug_print.h"

#include "sdk_defs.h"

#include <stdint.h>

//! Implementation for the bootloader.
//! Because ASSERT_ERROR() and ASSERT_WARNING() macros in sdk_defs.h use this function,
//! we need an implementation for the bootloader as well.
NORETURN passert_failed_no_message(void) {
  debug_print_str_and_int("ASRT", (int)__builtin_return_address(0));
#if NO_WATCHDOG
  while (1) {};
#else
  NVIC_SystemReset();
#endif
  __builtin_unreachable();
}
