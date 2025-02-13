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

#include "system/hexdump.h"
#include "system/logging.h"
#include "util/hexdump.h"

#include <string.h>

extern int _write(int file, char *ptr, int len);

static void prv_retarget_write_line_cb(int level, const char *src_filename, int src_line_number,
                                       const char *line_buffer) {
  PBL_LOG(LOG_LEVEL_DEBUG, "%s", line_buffer);
}

void hexdump_log_src(int level, const uint8_t *data, size_t length) {
  hexdump(NULL, 0, level, data, length, prv_retarget_write_line_cb);
}
