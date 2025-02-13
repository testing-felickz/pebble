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

#include "system/passert.h"

#include "core_dump.h"
#include "die.h"
#include "reboot_reason.h"

#include <stdio.h>

NORETURN passert_failed_no_message(void) {
  passert_failed_no_message_with_lr((uint32_t)__builtin_return_address(0));
}

NORETURN passert_failed_no_message_with_lr(uint32_t lr) {
  RebootReason reason = {
    .code = RebootReasonCode_Assert,
    .extra = lr,
  };
  reboot_reason_set(&reason);

  printf("ASSERT! lr=0x%lx\n", lr);
  reset_due_to_software_failure();
}

NORETURN passert_rom_error_no_message_with_errortype(uint32_t error_type_stat) {
  RebootReason reason = {
    .code = RebootReasonCode_RomError,
    .extra = error_type_stat,
  };
  reboot_reason_set(&reason);

  printf("RomErr! stat=0x%lx\n", error_type_stat);
  reset_due_to_software_failure();
}
