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

#include "chip_id.h"

#include "bsp_definitions.h"
#include "da1468x_mem_map.h"
#include "hw_otpc.h"
#include "system/logging.h"

#include <stdio.h>
#include <string.h>

#define OTP_CELL_SIZE_BYTES (8)

static uint32_t prv_get_cell_offset_for_address(uintptr_t otp_addr) {
  return (otp_addr - MEMORY_OTP_BASE) / OTP_CELL_SIZE_BYTES;
}

static bool prv_read_otp_bytes(uintptr_t otp_addr, uint8_t *buffer_out, size_t length_bytes) {
  // Ceil!
  size_t num_words = (length_bytes + (sizeof(uint32_t) - 1)) / sizeof(uint32_t);

  // Ensure our accesses are aligned by using this temporary buffer:
  uint32_t temp_buffer[num_words];
  bool rv = hw_otpc_fifo_read(temp_buffer, prv_get_cell_offset_for_address(otp_addr),
                              HW_OTPC_WORD_LOW, num_words, false /* spare_rows */);
  memcpy(buffer_out, temp_buffer, length_bytes);

  if (!rv) {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to read OTP %p", (void *)otp_addr);
  }

  return rv;
}

bool dialog_chip_id_copy(DialogChipID *chip_id_out) {
  hw_otpc_init();

#if dg_configEXT_CRYSTAL_FREQ == EXT_CRYSTAL_IS_16M
  HW_OTPC_SYS_CLK_FREQ freq = HW_OTPC_SYS_CLK_FREQ_16;
#elif dg_configEXT_CRYSTAL_FREQ == EXT_CRYSTAL_IS_32M
  HW_OTPC_SYS_CLK_FREQ freq = HW_OTPC_SYS_CLK_FREQ_32;
#else
#  error "Unsupported sysclk frequency"
#endif

  hw_otpc_set_speed(freq);

  // Read the position/package/timestamp info:
  bool rv = prv_read_otp_bytes(OTP_POS_PKG_TIMESTAMP_ADDRESS, (uint8_t *)&chip_id_out->info,
                               sizeof(chip_id_out->info));

  // Read the chip identifier string:
  rv &= prv_read_otp_bytes(OTP_CHIP_ID_ADDRESS, (uint8_t *)&chip_id_out->chip_id,
                           sizeof(chip_id_out->chip_id));

  // Zero terminate, just in case:
  chip_id_out->chip_id[sizeof(chip_id_out->chip_id) - 1] = '\0';

  hw_otpc_disable();

  return rv;
}
