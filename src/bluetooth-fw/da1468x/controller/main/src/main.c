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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "chip_id.h"
#include "hc_protocol/hc_protocol.h"
#include "host_transport_impl.h"
#include "kernel/pbl_malloc.h"
#include "power.h"
#include "system/logging.h"
#include "tasks.h"

#include <os/mutex.h>
#include <os/tick.h>

// Dialog SDK:
#include "ad_ble.h"
#include "ad_nvms.h"
#include "ad_spi.h"
#include "ble_mgr.h"
#include "hw_gpio.h"
#include "hw_watchdog.h"
#include "osal.h"
#include "sys_clock_mgr.h"
#include "sys_power_mgr.h"
#include "sys_watchdog.h"

/* The configCHECK_FOR_STACK_OVERFLOW setting in FreeRTOSConifg can be used to
check task stacks for overflows.  It does not however check the stack used by
interrupts.  This demo has a simple addition that will also check the stack used
by interrupts if mainCHECK_INTERRUPT_STACK is set to 1.  Note that this check is
only performed from the tick hook function (which runs in an interrupt context).
It is a good debugging aid - but won't catch interrupt stack problems until the
tick interrupt next executes. */
// #define mainCHECK_INTERRUPT_STACK 1
#if mainCHECK_INTERRUPT_STACK == 1
const unsigned char ucExpectedInterruptStackValues[] = {
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
};
#endif


/*
 * Perform any application specific hardware configuration.  The clocks,
 * memory, etc. are configured before main() is called.
 */
static void prvSetupHardware( void );
/*
 * Task functions .
 */

static void prv_print_banner(bool pre_host_logging) {
  if (pre_host_logging) {
    PBL_LOG(LOG_LEVEL_ALWAYS, "*** Main BLE FW booting... (pre-host logging) ***");
  } else {
    PBL_LOG(LOG_LEVEL_ALWAYS, "*** Main BLE Controller booted in %d ms... ***",
            (int)ticks_to_milliseconds(xTaskGetTickCount()));
  }

  DialogChipID chip_id = {};
  dialog_chip_id_copy(&chip_id);
  PBL_LOG(LOG_LEVEL_DEBUG,
          "Chip ID='%s', ts=%"PRIu32", pkg=0x%"PRIx8", wafer=%"PRIu8", x=%"PRIu8", y=%"PRIu8"",
          chip_id.chip_id, chip_id.info.timestamp, chip_id.info.package_type,
          chip_id.info.wafer_number, chip_id.info.x_coord, chip_id.info.y_coord);
}

/**
 * @brief System Initialization and creation of the BLE task
 */
static void system_init( void *pvParameters ) {
  /* Prepare clocks. Note: cm_cpu_clk_set() and cm_sys_clk_set() can be called only from a
   * task since they will suspend the task until the XTAL16M has settled and, maybe, the PLL
   * is locked.
   */
  cm_sys_clk_init(sysclk_XTAL16M);
  cm_apb_set_clock_divider(apb_div1);
  cm_ahb_set_clock_divider(ahb_div1);
  cm_lp_clk_init();

  /* Prepare the hardware to run this demo. */
  prvSetupHardware();

  /* Set system clock */
  cm_sys_clk_set(sysclk_XTAL16M);

  // Set the desired sleep mode.
  // It's important to do this *AFTER* setting up the system clock. This is because cm_sys_clk_set()
  // has the potential to block this task while waiting for the xtal to settle. When using deep
  // sleep, you'd fall into an eternal sleep because the "xtal ready" interrupt doesn't wake it up.
  power_init();

  pbl_log_init();

  prv_print_banner(true);

  // This registers the power manager callbacks for the SPI adapter.
  ad_spi_init();

  /* Initialize BLE Adapter */
  ad_ble_init();

  /* Initialize BLE Manager */
  ble_mgr_init();

  host_transport_init();

  prv_print_banner(false);

  /* the work of the SysInit task is done */
  task_unregister_task(OS_GET_CURRENT_TASK());
  OS_TASK_DELETE(OS_GET_CURRENT_TASK());
}

/*-----------------------------------------------------------*/

/**
 * @brief Basic initialization and creation of the system initialization task.
 */
uint8_t *ucHeap;
static void prv_init_main_heap(void) {
  // We get the location of the heap from a variable provided by the linker script.
  // The heap is zero-initialized by startup_ARMCM0.S.
  extern uint8_t __heap_start;
  ucHeap = &__heap_start;
}

int main( void ) {
  prv_init_main_heap();

  OS_BASE_TYPE status;

  cm_clk_init_low_level();                            /* Basic clock initializations. */

  /* Initialize BLE Heap */
  kernel_heap_init();

  sys_watchdog_init();

  /* Start SysInit task. */
  status = OS_TASK_CREATE("SI",                      /* The text name assigned to the task, for
                                                       debug only; not used by the kernel. */
                          system_init,              /* The System Initialization task. */
                          ( void * ) 0,             /* The parameter passed to the task. */
                          1024,                     /* The number of bytes to allocate to the
                                                       stack of the task. */
                          configMAX_PRIORITIES - 1, /* The priority assigned to the task. */
                          DialogTaskList[DialogTask_SysInit]); /* The task handle */
  configASSERT(status == OS_TASK_CREATE_SUCCESS);

  /* Start the tasks and timer running. */

  vTaskStartScheduler();

  /* If all is well, the scheduler will now be running, and the following
     line will never be reached.  If the following line does execute, then
     there was insufficient FreeRTOS heap memory available for the idle and/or
     timer tasks     to be created.  See the memory management section on the
     FreeRTOS web site for more details. */
  for( ;; );
}


static void prv_periph_setup(void) {
  host_transport_init_periph();
}

static void prvSetupHardware( void )
{
#if mainCHECK_INTERRUPT_STACK == 1
  extern unsigned long _vStackTop[], _pvHeapStart[];
  unsigned long ulInterruptStackSize;
#endif

  /* Init hardware */
  pm_system_init(prv_periph_setup);

#if mainCHECK_INTERRUPT_STACK == 1
  /* The size of the stack used by main and interrupts is not defined in
     the linker, but just uses whatever RAM is left.  Calculate the amount of
     RAM available for the main/interrupt/system stack, and check it against
     a reasonable number.  If this assert is hit then it is likely you don't
     have enough stack to start the kernel, or to allow interrupts to nest.
     Note - this is separate to the stacks that are used by tasks.  The stacks
     that are used by tasks are automatically checked if
     configCHECK_FOR_STACK_OVERFLOW is not 0 in FreeRTOSConfig.h - but the stack
     used by interrupts is not.  Reducing the conifgTOTAL_HEAP_SIZE setting will
     increase the stack available to main() and interrupts. */
  ulInterruptStackSize = ( ( unsigned long ) _vStackTop ) - ( ( unsigned long ) _pvHeapStart );
  OS_ASSERT( ulInterruptStackSize > 350UL );

  /* Fill the stack used by main() and interrupts to a known value, so its
     use can be manually checked. */
  memcpy( ( void * ) _pvHeapStart, ucExpectedInterruptStackValues, sizeof( ucExpectedInterruptStackValues ) );
#endif
}
