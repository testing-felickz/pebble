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

#include "debug_print.h"
#include "hw_watchdog.h"
#include "sdk_defs.h"

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

static void prv_handle_fault(void) {
#if NO_WATCHDOG
  while (1) {};
#else
  NVIC_SystemReset();
#endif
}

void HardFault_HandlerC(uint32_t *fault_args) {
  debug_print_str("Hard fault");
  prv_handle_fault();
}

void UsageFault_HandlerC(uint32_t *fault_args) {
  debug_print_str("Usage fault");
  prv_handle_fault();
}

void BusFault_HandlerC(uint32_t *fault_args) {
  debug_print_str("Bus fault");
  prv_handle_fault();
}

void MemManag_HandlerC(uint32_t *fault_args) {
  debug_print_str("MemManag fault");
  prv_handle_fault();
}

void NMI_HandlerC(uint32_t *fault_args) {
  debug_print_str("NMI Handler");
  prv_handle_fault();
}
