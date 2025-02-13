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
#include "system/logging.h"
#include "system/passert.h"
#include "util/attributes.h"

#include <stdio.h>
#include <stdlib.h>

void os_log(const char *filename, int line, const char *string) {
  PBL_LOG(LOG_LEVEL_DEBUG, "%s:%d> %s", filename, line, string);
}

NORETURN os_assertion_failed(const char *filename, int line) {
  const uintptr_t lr = (uintptr_t) __builtin_return_address(0);
  PBL_ASSERTN_LR(0, lr);
}

NORETURN os_assertion_failed_lr(const char *filename, int line, uint32_t lr) {
  PBL_ASSERTN_LR(0, lr);
}

void *os_malloc(size_t size) {
  return kernel_malloc(size);
}

void *os_malloc_check(size_t size) {
  return kernel_malloc_check(size);
}

void os_free(void *ptr) {
  kernel_free(ptr);
}
