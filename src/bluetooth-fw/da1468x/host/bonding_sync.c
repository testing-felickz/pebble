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

#include "hc_protocol/hc_endpoint_bonding_sync.h"

#include "comm/ble/gatt_client_discovery.h"
#include "bonding_flags.h"

#include <bluetooth/bonding_sync.h>
#include <bluetooth/sm_types.h>

#include "system/hexdump.h"
#include "system/logging.h"

static void prv_dump_bonding(const BleBonding *bonding) {
#ifndef RELEASE
  PBL_LOG(LOG_LEVEL_INFO, "Dumping pairing info (LTK, Rand, Ediv, IRK):");
  PBL_HEXDUMP(LOG_LEVEL_INFO, (const uint8_t *)&bonding->pairing_info.local_encryption_info.ltk,
              sizeof(bonding->pairing_info.local_encryption_info.ltk));
  PBL_HEXDUMP(LOG_LEVEL_INFO, (const uint8_t *)&bonding->pairing_info.local_encryption_info.rand,
              sizeof(bonding->pairing_info.local_encryption_info.rand));
  PBL_HEXDUMP(LOG_LEVEL_INFO, (const uint8_t *)&bonding->pairing_info.local_encryption_info.ediv,
              sizeof(bonding->pairing_info.local_encryption_info.ediv));
  PBL_HEXDUMP(LOG_LEVEL_INFO, (const uint8_t *)&bonding->pairing_info.irk,
              sizeof(bonding->pairing_info.irk));
#endif
}

void bt_driver_handle_host_added_bonding(const BleBonding *bonding) {
  hc_endpoint_bonding_sync_add(bonding);
  prv_dump_bonding(bonding);
}

void bt_driver_handle_host_removed_bonding(const BleBonding *bonding) {
  hc_endpoint_bonding_sync_remove(bonding);
}

void bonding_sync_handle_hc_add(const BleBonding *bonding) {
  // By this time, Dialog is using the identity address to refer to the connection
  // (instead of the actual connection address):
  const BTDeviceInternal *address = &bonding->pairing_info.identity;
  bt_driver_cb_handle_create_bonding(bonding, &address->address);
  prv_dump_bonding(bonding);

  if (bonding->flags & BleBondingFlag_IsReversedPPoGATTEnabled) {
    // Trigger GATT service rediscovery to find the emulated PPoGATT server
    gatt_client_discovery_discover_all(address);
  }
}

void bonding_sync_handle_hc_remove(const BleBonding *bonding) {
  PBL_LOG(LOG_LEVEL_ERROR, "Controller isn't supposed to remove bondings!");
}
