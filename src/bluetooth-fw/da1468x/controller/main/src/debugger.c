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

#include "debugger.h"

#include "hw_cpm.h"
#include "hw_watchdog.h"
#include "sdk_defs.h"

#include <stdio.h>

#if NO_WATCHDOG
void debugger_await(void) {
  hw_watchdog_freeze();
  hw_cpm_enable_debugger();

  printf("\nAttach gdb and enter:\n"
         "(gdb) set $r0 = 0\n"
         "(gdb) si\n"
         "(gdb) <hit return a few times to return to the point where the fault happened>\n");

  NVIC_ClearPendingIRQ(HardFault_IRQn);
  __asm("mov r0, #1");
  while (true) {
    register uintptr_t r0 __asm("r0");
    if (r0 == 0) {
      break;
    }
  }
}
#endif
