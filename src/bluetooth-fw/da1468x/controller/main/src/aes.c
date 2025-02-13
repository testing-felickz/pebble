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

#include "aes.h"

#include "system/hexdump.h"
#include "system/logging.h"
#include "system/passert.h"

// Dialog SDK:
#include "hw_aes_hash.h"
#include "sdk_defs.h"

#include <string.h>

bool aes_128_encrypt_block(const uint8_t key[AES_128_KEY_SIZE],
                           const uint8_t plain_text_block[AES_128_BLOCK_SIZE],
                           uint8_t cipher_text_block_out[AES_128_BLOCK_SIZE]) {

  // Nothing else should currently be using the crypto block, just in case this changes:
  PBL_ASSERTN(!REG_GETF(CRG_TOP, CLK_AMBA_REG, AES_CLK_ENABLE));

  hw_aes_hash_setup aes_setup = {
    .mode = HW_AES_ECB, // we're just encrypting one block, so just use "code book"
    .aesDirection = HW_AES_ENCRYPT,
    .aesKeySize = HW_AES_128,
    .aesKeyExpand = true,
    .aesKeys = (uintptr_t)(void *)key,
    .aesWriteBackAll = false,
    .moreDataToCome = false,
    .sourceAddress = (uintptr_t)(void *)plain_text_block,
    .destinationAddress = (uintptr_t)(void *)cipher_text_block_out,
    .dataSize = AES_128_BLOCK_SIZE,
    .enableInterrupt = false,
  };
  hw_aes_hash_init(&aes_setup);
  hw_aes_hash_start();
  hw_aes_hash_disable(true /* wait_until_inactive */);

  return true;
}
