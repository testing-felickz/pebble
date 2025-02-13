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

#include "die.h"

#include "debugger.h"
#include "core_dump.h"

NORETURN reset_due_to_software_failure(void) {
#if NO_WATCHDOG
  debugger_await();
  while (1) {};
  __builtin_unreachable();
#else
  core_dump(false);
#endif
}

void core_dump_and_reset_or_reboot(void) {
  reset_due_to_software_failure();
}
