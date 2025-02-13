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

#pragma once

#include <util/attributes.h>
#include <util/likely.h>

#include <stdint.h>

NORETURN passert_failed_no_message(void);

NORETURN passert_failed_no_message_with_lr(uint32_t lr);

NORETURN passert_rom_error_no_message_with_errortype(uint32_t error_type_stat);

#define PBL_ASSERTN(expr) \
  do { \
    if (UNLIKELY(!(expr))) { \
      passert_failed_no_message(); \
    } \
  } while (0)


#define PBL_ASSERTN_LR(expr, lr) \
  do { \
    if (UNLIKELY(!(expr))) { \
      passert_failed_no_message_with_lr(lr); \
    } \
  } while (0)

#define WTF PBL_ASSERTN(0)
