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
#include <stdbool.h>
#include <stdint.h>

// Log domains

#define LOG_DOMAIN_MISC                    1

#ifndef LOG_DOMAIN_GATT_DEBUG
#define LOG_DOMAIN_GATT_DEBUG              0
#endif

void pbl_log_init(void);
void pbl_log_hashed(const uint32_t packed_loghash, ...);
void pbl_log(uint8_t log_level, const char *src_filename, int src_line_number,
             const char *fmt, ...) FORMAT_PRINTF(4, 5);

void pbl_log_set_level(uint8_t level);
uint8_t pbl_log_get_level(void);

// Log defines/stubs

#define LOG_LEVEL_ALWAYS 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARNING 50
#define LOG_LEVEL_INFO 100
#define LOG_LEVEL_DEBUG 200
#define LOG_LEVEL_DEBUG_VERBOSE 255

#ifdef PBL_LOGS_HASHED
#define PBL_SHOULD_LOG(level) (true)
#else
#define PBL_SHOULD_LOG(level) (level <= LOG_LEVEL_ALWAYS)
#endif

#include "logging/log_hashing.h"

#define LOG_COLOR_BLACK          "BLACK"        // Not so useful in general
#define LOG_COLOR_RED            "RED"
#define LOG_COLOR_GREEN          "GREEN"
#define LOG_COLOR_YELLOW         "YELLOW"
#define LOG_COLOR_BLUE           "BLUE"
#define LOG_COLOR_MAGENTA        "MAGENTA"
#define LOG_COLOR_CYAN           "CYAN"
#define LOG_COLOR_GREY           "GREY"
// Reserved for bold. Use sparingly
#define LOG_COLOR_LIGHT_GREY     "LIGHT_GREY"
#define LOG_COLOR_LIGHT_RED      "LIGHT_RED"
#define LOG_COLOR_LIGHT_GREEN    "LIGHT_GREEN"
#define LOG_COLOR_LIGHT_YELLOW   "LIGHT_YELLOW"
#define LOG_COLOR_LIGHT_BLUE     "LIGHT_BLUE"
#define LOG_COLOR_LIGHT_MAGENTA  "LIGHT_MAGENTA"
#define LOG_COLOR_LIGHT_CYAN     "LIGHT_CYAN"
#define LOG_COLOR_WHITE          "WHITE"

// Allow the default color for a src file to be set by defining FILE_LOG_COLOR
// Can be called directly with PBL_LOG_COLOR
#ifdef FILE_LOG_COLOR
  #define DEFAULT_LOG_COLOR        FILE_LOG_COLOR
#else
  #define DEFAULT_LOG_COLOR        LOG_COLOR_GREY
#endif


#define SPLIT_64_BIT_ARG(x) (uint32_t)((x >> 32) & 0xFFFFFFFF), (uint32_t)(x & 0xFFFFFFFF)

#ifndef DEFAULT_LOG_DOMAIN
  #define DEFAULT_LOG_DOMAIN LOG_DOMAIN_MISC
#endif // DEFAULT_LOG_DOMAIN

#ifdef PBL_LOGS_HASHED

#define PBL_LOG_D(domain, level, fmt, ...)                       \
  do { \
    if (PBL_SHOULD_LOG(level)) { \
      if (domain) { \
        NEW_LOG_HASH(pbl_log_hashed, level, DEFAULT_LOG_COLOR, fmt, ## __VA_ARGS__); \
      } \
    } \
  } while (0)

#define PBL_LOG_COLOR_D(domain, level, color, fmt, ...)                       \
  do { \
    if (PBL_SHOULD_LOG(level)) { \
      if (domain) { \
        NEW_LOG_HASH(pbl_log_hashed, level, color, fmt, ## __VA_ARGS__); \
      } \
    } \
  } while (0)

#else // PBL_LOGS_HASHED

#define PBL_LOG_D(domain, level, fmt, ...)                       \
  do { \
    if (PBL_SHOULD_LOG(level)) { \
      if (domain) { \
        pbl_log(level, __FILE_NAME__, __LINE__, fmt, ## __VA_ARGS__); \
      } \
    } \
  } while (0)

#define PBL_LOG_COLOR_D(domain, level, color, fmt, ...)                       \
  do { \
    if (PBL_SHOULD_LOG(level)) { \
      if (domain) { \
        pbl_log(level, __FILE_NAME__, __LINE__, fmt, ## __VA_ARGS__); \
      } \
    } \
  } while (0)

#endif // PBL_LOGS_HASHED

#define PBL_LOG(level, fmt, ...)                                \
  PBL_LOG_D(DEFAULT_LOG_DOMAIN, level, fmt, ## __VA_ARGS__)

#define PBL_LOG_COLOR(level, color, fmt, ...)           \
  PBL_LOG_COLOR_D(DEFAULT_LOG_DOMAIN, level, color, fmt, ## __VA_ARGS__)

#define GATT_LOG_DEBUG(fmt, args...) PBL_LOG_D(LOG_DOMAIN_GATT_DEBUG, LOG_LEVEL_DEBUG, fmt, ## args)

