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

#include "kernel/pbl_malloc.h"

#include "system/passert.h"
#include "util/attributes.h"
#include "util/heap.h"

#include "FreeRTOS.h"
#include "sdk_defs.h"

#include <string.h>

static Heap s_kernel_heap;
static bool s_interrupts_disabled_by_heap;

// FIXME: Cortex-M0 does not have the CMSIS, therefore can't disable interrupts for lower priority
// tasks. It's all or nothing. We should come up with a better way for this.
static void prv_heap_lock(void *ctx) {
  if ((__get_PRIMASK() & 0x1) == 0) {
    __disable_irq();
    s_interrupts_disabled_by_heap = true;
  }
}

static void prv_heap_unlock(void *ctx) {
  if (s_interrupts_disabled_by_heap) {
    __enable_irq();
    s_interrupts_disabled_by_heap = false;
  }
}

void kernel_heap_init(void) {
  const bool fuzz_on_free = true;

  extern uint8_t __heap_start;
  uint8_t *heap_start = &__heap_start;
  uint8_t *heap_end = heap_start + configTOTAL_HEAP_SIZE;
  heap_init(&s_kernel_heap, heap_start, heap_end, fuzz_on_free);
  heap_set_lock_impl(&s_kernel_heap, (HeapLockImpl) {
    .lock_function = prv_heap_lock,
    .unlock_function = prv_heap_unlock
  });
}

// kernel_* functions that allocate on the kernel heap
///////////////////////////////////////////////////////////

static ALWAYS_INLINE void *prv_heap_malloc(size_t bytes) {
  const uintptr_t saved_lr = (uintptr_t) __builtin_return_address(0);
  return heap_malloc(&s_kernel_heap, bytes, saved_lr);
}

void *kernel_malloc(size_t bytes) {
  return prv_heap_malloc(bytes);
}

void *kernel_zalloc(size_t bytes) {
  void *ptr = prv_heap_malloc(bytes);
  if (ptr) {
    memset(ptr, 0, bytes);
  }
  return ptr;
}

void *kernel_malloc_check(size_t bytes) {
  void *ptr = prv_heap_malloc(bytes);
  PBL_ASSERTN(ptr);
  return ptr;
}

void *kernel_zalloc_check(size_t bytes) {
  void *ptr = prv_heap_malloc(bytes);
  PBL_ASSERTN(ptr);
  memset(ptr, 0, bytes);
  return ptr;
}

void kernel_free(void *ptr) {
  register uintptr_t lr __asm("lr");
  uintptr_t saved_lr = lr;

  heap_free(&s_kernel_heap, ptr, saved_lr);
}

// Fun fact: The unconditional branch range for cortex M0 is very small (See:
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0497a/BABEFHAE.html)
// This seems to result in the compiler always emitting "bl" instructions even
// if a function is just calling one other function. Thus, just alias the
// following routines to the function they call so that we get the lr from the
// original call site saved.
ALIAS("kernel_malloc") void* pvPortMalloc(size_t xSize);
ALIAS("kernel_free") void vPortFree(void* pv);
