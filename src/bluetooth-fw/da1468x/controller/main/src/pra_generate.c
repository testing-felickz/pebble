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

#include "pra_generate.h"

#include "aes.h"
#include "system/hexdump.h"
#include "system/logging.h"

// Dialog SDK:
#include "ble_mgr.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/sm_types.h>
#include <util/attributes.h>

#include <stdlib.h>
#include <string.h>

// For some reason, the AES bytes seem to be reversed within the BLE spec?
// See for example BT Spec v4.2, Vol 2. Part E, 7.8.22 "HCI LE Encrypt":
// "The most significant octet of the key corresponds to key[0] using the notation specified
// in FIPS 197."
static void prv_reverse_byte_order(uint8_t block[AES_128_BLOCK_SIZE]) {
  for (uint32_t i = 0; i < AES_128_BLOCK_SIZE / 2; ++i) {
    uint8_t *a = &block[i];
    uint8_t *b = &block[AES_128_BLOCK_SIZE - i - 1];
    uint8_t temp = *a;
    *a = *b;
    *b = temp;
  }
}

static bool prv_emulate_hci_le_encrypt(const SM128BitKey *key,
                                       const uint8_t plain_text_block[AES_128_BLOCK_SIZE],
                                       uint8_t cipher_text_block_out[AES_128_BLOCK_SIZE]) {
  const SM128BitKey key_reversed = *key;
  prv_reverse_byte_order((uint8_t *)&key_reversed);

  uint8_t plain_text_block_reversed[AES_128_BLOCK_SIZE];
  memcpy(plain_text_block_reversed, plain_text_block, sizeof(plain_text_block_reversed));
  prv_reverse_byte_order(plain_text_block_reversed);

  bool rv = aes_128_encrypt_block((const uint8_t *)&key_reversed, plain_text_block_reversed,
                                  cipher_text_block_out);

  prv_reverse_byte_order(cipher_text_block_out);
  return rv;
}

void pra_generate(BTDeviceAddress *address_out) {
  SMIdentityResolvingKey irk;
  const ble_dev_params_t *dev_params = ble_mgr_dev_params_acquire();
  memcpy(irk.data, dev_params->irk.key, sizeof(irk.data));
  ble_mgr_dev_params_release();

  // rand() returns between 0 and RAND_MAX, but only the 24 LSBits are used anyway.
  const int prand = rand();

  pra_generate_with_prand_and_irk(address_out, prand, &irk);
}

void pra_generate_with_prand_and_irk(BTDeviceAddress *address_out,
                                     uint32_t prand, const SMIdentityResolvingKey *irk) {
  // See Core v4.2, Vol 6, Part B, "1.3.2.2 Private Device Address Generation"

  const size_t HASH_LEN_BYTES = (24 / 8);
  const size_t PRAND_LEN_BYTES = (24 / 8);

  uint8_t prand_bytes[AES_128_BLOCK_SIZE];
  memset(prand_bytes, 0, sizeof(prand_bytes));
  // Copy the 24 LSBits of prand:
  memcpy(prand_bytes, &prand, PRAND_LEN_BYTES);
  // Assign the 2 MSBits of the prand, these are used to indicate the address is a PRA.
  prand_bytes[PRAND_LEN_BYTES - 1] &= 0b00111111;
  prand_bytes[PRAND_LEN_BYTES - 1] |= 0b01000000;

  uint8_t hash_bytes[AES_128_BLOCK_SIZE];
  prv_emulate_hci_le_encrypt(irk, prand_bytes, hash_bytes);

  // Use 24 LSBits of the cipher text as 24 LSBits of the address:
  memcpy(&address_out->octets[0], hash_bytes, HASH_LEN_BYTES);
  // Copy the prand to the 24 MSBits of the address:
  memcpy(&address_out->octets[HASH_LEN_BYTES], prand_bytes, PRAND_LEN_BYTES);
}
