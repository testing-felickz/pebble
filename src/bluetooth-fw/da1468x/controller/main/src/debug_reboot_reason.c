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

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#include "reboot_reason.h"
#include "system/logging.h"

void debug_reboot_reason_print(void) {
  RebootReason reason;
  if (!reboot_reason_get(&reason)) {
    PBL_LOG(LOG_LEVEL_DEBUG, "Dialog: Invalid reboot reason");
    goto clear;
  };

  switch (reason.code) {
    // Normal stuff
    case RebootReasonCode_Unknown:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to Unknown.");
      break;
    case RebootReasonCode_Shutdown:
      // FIXME PBL-38181: For some reason, the reboot reason shutdown never gets persisted/recovered
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to Shutdown.");
      break;
    // Error occurred
    case RebootReasonCode_Watchdog:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to watchdog, stuck task mask: 0x%"PRIx32,
              reason.extra);
      break;
    case RebootReasonCode_Assert:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to Assert: LR 0x%"PRIx32, reason.extra);
      break;
    case RebootReasonCode_HardFault:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to HardFault: LR 0x%"PRIx32, reason.extra);
      break;
    case RebootReasonCode_StackOverflow:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to StackOverflow");
      break;
    case RebootReasonCode_DialogPlatformReset:
      PBL_LOG(LOG_LEVEL_ALWAYS,
              "Dialog: Rebooted due to DialogPlatformReset: error 0x%"PRIx32, reason.extra);
      break;
    case RebootReasonCode_CoreDumpReentered:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Re-entered core dump. Possibly valid -> LR 0x%"PRIx32,
              reason.extra);
      break;
    case RebootReasonCode_CoreDumpRequested:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: User requested core dump");
      break;
    case RebootReasonCode_RomError:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to RomError: ERRORTYPESTAT 0x%"PRIx32,
              reason.extra);
      break;
    case RebootReasonCode_NMI:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to NMI");
      break;
    default:
      PBL_LOG(LOG_LEVEL_ALWAYS, "Dialog: Rebooted due to Unrecognized Reason");
      break;
  }

clear:
  reboot_reason_clear();
}
