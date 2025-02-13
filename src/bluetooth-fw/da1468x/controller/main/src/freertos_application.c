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

#include <stdio.h>

#include "FreeRTOS.h"
#include "osal.h"

#include "kernel/pbl_malloc.h"
#include "reboot_reason.h"
#include "util/heap.h"

/**
 * @brief Malloc fail hook
 */
void vApplicationMallocFailedHook( void )
{
  /* vApplicationMallocFailedHook() will only be called if
     configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
     function that will get called if a call to OS_MALLOC() fails.
     OS_MALLOC() is called internally by the kernel whenever a task, queue,
     timer or semaphore is created.  It is also called by various parts of the
     demo application.  If heap_1.c or heap_2.c are used, then the size of the
     heap available to OS_MALLOC() is defined by configTOTAL_HEAP_SIZE in
     FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
     to query the size of free heap space that remains (although it does not
     provide information on how the remaining heap might be fragmented). */
  taskDISABLE_INTERRUPTS();
}

/**
 * @brief Application idle task hook
 */
void vApplicationIdleHook( void )
{
  /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
     to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
     task. It is essential that code added to this hook function never attempts
     to block in any way (for example, call OS_QUEUE_GET() with a block time
     specified, or call OS_DELAY()).  If the application makes use of the
     OS_TASK_DELETE() API function (as this demo application does) then it is also
     important that vApplicationIdleHook() is permitted to return to its calling
     function, because it is the responsibility of the idle task to clean up
     memory allocated by the kernel to any task that has since been deleted. */
}

/**
 * @brief Application stack overflow hook
 */
void vApplicationStackOverflowHook( OS_TASK pxTask, char *pcTaskName )
{
  ( void ) pcTaskName;
  ( void ) pxTask;

  /* Run time stack overflow checking is performed if
     configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
     function is called if a stack overflow is detected. */
  taskDISABLE_INTERRUPTS();

  printf("SO on %s", pcTaskName);

  // TODO: Register which stack had the overflow.
  RebootReason reason = {
    .code = RebootReasonCode_StackOverflow,
  };
  reboot_reason_set(&reason);

  while (1) {};
}

/**
 * @brief Application tick hook
 */
void vApplicationTickHook( void )
{
#if mainCHECK_INTERRUPT_STACK == 1
  extern unsigned long _pvHeapStart[];

  /* This function will be called by each tick interrupt if
     configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
     added here, but the tick hook is called from an interrupt context, so
     code must not attempt to block, and only the interrupt safe FreeRTOS API
     functions can be used (those that end in FromISR()). */

  /* Manually check the last few bytes of the interrupt stack to check they
     have not been overwritten.  Note - the task stacks are automatically
     checked for overflow if configCHECK_FOR_STACK_OVERFLOW is set to 1 or 2
     in FreeRTOSConifg.h, but the interrupt stack is not. */
  OS_ASSERT( memcmp( ( void * ) _pvHeapStart, ucExpectedInterruptStackValues, sizeof( ucExpectedInterruptStackValues ) ) == 0U );
#endif /* mainCHECK_INTERRUPT_STACK */
}

#ifdef JUST_AN_EXAMPLE_ISR

void Dummy_IRQHandler(void)
{
  /* Clear the interrupt if necessary. */
  Dummy_ClearITPendingBit();

  OS_EVENT_SIGNAL_FROM_ISR(xTestSemaphore);
}

#endif /* JUST_AN_EXAMPLE_ISR */
