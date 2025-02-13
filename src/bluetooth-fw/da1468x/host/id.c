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

#include "services/common/bluetooth/local_id.h"

#include <bluetooth/id.h>
#include <bluetooth/bluetooth_types.h>

#include <stdio.h>
#include <string.h>

#include "hc_protocol/hc_endpoint_gap_service.h"
#include "hc_protocol/hc_endpoint_chip_id.h"

void bt_driver_id_set_local_device_name(const char device_name[BT_DEVICE_NAME_BUFFER_SIZE]) {
  hc_endpoint_gap_service_set_dev_name(device_name);
}

//! Generates a static address
//!
//! A static address is a 48-bit randomly generated address and shall meet the following
//! requirements:
//! - The two most significant bits of the static address shall be equal to '1'
//! - All bits of the random part of the static address shall not be equal to '1'
//! - All bits of the random part of the static address shall not be equal to '0'
//!
void bt_driver_id_copy_local_identity_address(BTDeviceAddress *addr_out) {
  bt_local_id_generate_address_from_serial(addr_out);
}

void bt_driver_set_local_address(bool allow_cycling,
                                 const BTDeviceAddress *pinned_address) {
  hc_endpoint_gap_service_set_local_address(allow_cycling, pinned_address);
}

void bt_driver_id_copy_chip_info_string(char *dest, size_t dest_size) {
  DialogChipID chip_id;
  if (!hc_endpoint_chip_id_query_chip_info(&chip_id)) {
    strncpy(dest, "?", dest_size);
    return;
  }

  // Use hex string of chip_id as unique id:
  uint8_t *chip_id_bytes = (uint8_t *)&chip_id;
  for (uint32_t i = 0; dest_size && i < sizeof(DialogChipID); ++i, dest_size -= 2, dest += 2) {
    sprintf(dest, "%02X", chip_id_bytes[i]);
  }
}

bool bt_driver_id_generate_private_resolvable_address(BTDeviceAddress *address_out) {
  return hc_endpoint_gap_service_generate_private_resolvable_address(address_out);
}
