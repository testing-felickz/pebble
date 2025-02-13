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

#include "reboot_reason.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "util/attributes.h"

static RebootReason s_reboot_reason SECTION("reboot_reason");

static const uint32_t COOKIE = 0xdeadbeef;

void reboot_reason_set(RebootReason *reason) {
  reason->cookie = COOKIE;
  s_reboot_reason = *reason;
}

bool reboot_reason_get(RebootReason *reason) {
  *reason = s_reboot_reason;
  return (reason->cookie == COOKIE);
}

void reboot_reason_clear(void) {
  s_reboot_reason = (RebootReason){};
}

uint32_t reboot_reason_get_crash_lr(void) {
  RebootReason reason;
  if (!reboot_reason_get(&reason)) {
    return 0;
  };

  switch (reason.code) {
    case RebootReasonCode_Assert:
    case RebootReasonCode_HardFault:
    case RebootReasonCode_RomError:
    case RebootReasonCode_Watchdog:
      return reason.extra;
    default:
      return 0;
  }
}

RebootReasonCode reboot_reason_get_last_reboot_reason(void) {
  RebootReason reason;
  if (!reboot_reason_get(&reason)) {
    return RebootReasonCode_Unknown;
  };
  return reason.code;
}
