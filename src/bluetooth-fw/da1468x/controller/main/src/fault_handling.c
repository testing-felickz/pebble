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

#include "hw_watchdog.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "core_cm0.h"
#include "core_dump.h"
#include "die.h"
#include "reboot_reason.h"
#include "sdk_defs.h"
#include "sys_watchdog.h"
#include "core_dump.h"

uint32_t *g_stacked_regs = NULL;

static void prv_handle_fault(void) {
  // Feed the watchdog before we continue. NMI will be triggered very soon if we don't.
  // Don't freeze the watchdog. It's better if the core-dump code goes into low-power mode than
  // crash and drain the battery.
  hw_watchdog_set_pos_val(0xFF);

  printf("R0=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_R0]);
  printf("R1=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_R1]);
  printf("R2=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_R2]);
  printf("R3=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_R3]);
  printf("R12=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_R12]);
  printf("LR=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_LR]);
  printf("PC=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_PC]);
  printf("xPSR=0x%08" PRIx32 "\n", g_stacked_regs[Stacked_Register_xPSR]);
  printf("SP=0x%08" PRIx32 "\n", (uint32_t) g_stacked_regs); // Stack Pointer

  reset_due_to_software_failure();
}

static bool prv_handle_hardfault_due_to_bkpt(void) {
  // Note: assumes Thumb2!
  const uint16_t instr = *(const uint16_t *)g_stacked_regs[Stacked_Register_PC];
  const uint16_t bkpt_instruction_opcode = 0xbe;
  if ((instr >> 8) != bkpt_instruction_opcode) {
    return false;
  }
  const uint8_t arg = (instr & 0xff);

  printf("\nbkpt %"PRIu8"! no dbgr?\n\n", arg);
  return true;
}

void HardFault_HandlerC(uint32_t *fault_args) {
  // Save the fault_args pointer for later
  g_stacked_regs = fault_args;

  bool is_bkpt = prv_handle_hardfault_due_to_bkpt();

  uint32_t stacked_lr = g_stacked_regs[Stacked_Register_LR];
  RebootReason reason = {
    .code = is_bkpt ? RebootReasonCode_BreakpointButNoDebuggerAttached : RebootReasonCode_HardFault,
    .extra = stacked_lr,
  };
  reboot_reason_set(&reason);

  printf("HardF:\n");
  prv_handle_fault();
}

void NMI_HandlerC(uint32_t *fault_args) {
  // Save the fault_args pointer for later
  g_stacked_regs = fault_args;

  uint32_t extra_reboot_info = 0;
  RebootReason reason;
  bool reboot_already_set = reboot_reason_get(&reason);
  RebootReasonCode code = RebootReasonCode_NMI;

  // Are we here because the hw watchdog tripped?
  if (hw_watchdog_did_trip()) {
    if (hw_watchdog_recovered()) {
      return;
    }

#if dg_configUSE_WDOG
    extra_reboot_info = watchdog_get_tasks_waiting_on();
    code = RebootReasonCode_Watchdog;
#endif
  }

  // chained faults could manifest in an NMI or watchdog. We are more interested in the crash that
  // happened first
  if (!reboot_already_set) {
    reason = (RebootReason){
      .code = code,
      .extra = extra_reboot_info
    };
    reboot_reason_set(&reason);
  }

  printf("NMI\n");
  prv_handle_fault();
}
