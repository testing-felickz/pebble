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

#include "system/logging.h"
#include "system/passert.h"

// Dialog SDK:
#include "hw_cpm.h"
#include "osal.h"
#include "sdk_defs.h"
#include "sys_power_mgr.h"

#if RELEASE
static OS_TIMER s_dbg_disable_timer;

#define DISABLE_DEBUG_DELAY_MS (15 * 1000)

static void prv_disable_debugger_cb(OS_TIMER timer) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Disabling debugger now to save power...");
  hw_cpm_disable_debugger();
  OS_TIMER_DELETE(s_dbg_disable_timer, OS_QUEUE_FOREVER);
  s_dbg_disable_timer = NULL;
}

static void prv_disable_debugger_with_delay(void) {
  s_dbg_disable_timer = OS_TIMER_CREATE("dbg", OS_MS_2_TICKS(DISABLE_DEBUG_DELAY_MS),
                                        false /* repeat */, NULL, prv_disable_debugger_cb);
  PBL_ASSERTN(s_dbg_disable_timer != NULL);
  PBL_ASSERTN(OS_TIMER_START(s_dbg_disable_timer, 0) == OS_TIMER_SUCCESS);
}
#endif

void power_init(void) {
  hw_cpm_enable_debugger();
#if RELEASE
  prv_disable_debugger_with_delay();
#endif

  pm_set_wakeup_mode(true);
  pm_set_sleep_mode(pm_mode_extended_sleep);
}

// Note: Once called, sleep mode will be disabled
void power_inhibit_sleep(void) {
  pm_set_sleep_mode(pm_mode_idle);
}

void power_enter_hibernation(void) {
  // Just this call alone goes a long way, because it will cause the system to enter hibernation
  // as soon as there are no more runnable tasks.
  pm_set_sleep_mode(pm_mode_hibernation);
}
