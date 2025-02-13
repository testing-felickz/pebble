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

#include <bluetooth/bonding_sync.h>
#include <bluetooth/bluetooth_types.h>
#include <bluetooth/sm_types.h>

#include <btutil/sm_util.h>

#include <string.h>

#include "bonding_flags.h"
#include "connection.h"
#include "dialog_utils.h"
#include "hc_protocol/hc_endpoint_bonding_sync.h"
#include "kernel/pbl_malloc.h"
#include "ppogatt_emulated_server_wa.h"
#include "system/logging.h"
#include "system/passert.h"

// Dialog SDK:
#include "ble_common.h"
#include "ble_gap.h"
#include "storage.h"

static key_ltk_t *prv_create_remote_ltk_from_info(const SMRemoteEncryptionInfo *enc_info) {
  key_ltk_t *ltk = kernel_zalloc_check(sizeof(key_ltk_t));
  ltk->rand = enc_info->rand;
  ltk->ediv = enc_info->ediv;
  memcpy(ltk->key, enc_info->ltk.data, sizeof(ltk->key));
  ltk->key_size = sizeof(ltk->key);
  return ltk;
}

static key_ltk_t *prv_create_local_ltk_from_info(const SMLocalEncryptionInfo *enc_info) {
  key_ltk_t *ltk = kernel_zalloc_check(sizeof(key_ltk_t));
  ltk->rand = enc_info->rand;
  ltk->ediv = enc_info->ediv;
  memcpy(ltk->key, enc_info->ltk.data, sizeof(ltk->key));
  ltk->key_size = sizeof(ltk->key);
  return ltk;
}

void bonding_sync_handle_hc_add(const BleBonding *bonding) {
  storage_acquire();
  const SMPairingInfo *info = &bonding->pairing_info;
  bd_address_t addr;
  dialog_utils_bt_device_to_bd_address(&bonding->pairing_info.identity, &addr);
  device_t *dev = find_device_by_addr(&addr, true /* create */);

  dev->paired = true;
  dev->bonded = true;
  dev->is_gateway = bonding->is_gateway;
  dev->flags = bonding->flags;
  dev->mitm = info->is_mitm_protection_enabled;

  // The LTK that's used when the local device is the slave.
  if (info->is_remote_encryption_info_valid) {
    dev->ltk = prv_create_remote_ltk_from_info(&info->remote_encryption_info);
  }

  // The LTK that's used when the local device is the master
  // (we call it "local", Dialog calls it "remote"... :-S )
  if (info->is_local_encryption_info_valid) {
    dev->remote_ltk = prv_create_local_ltk_from_info(&info->local_encryption_info);
  }

  if (info->is_remote_identity_info_valid) {
    if (sm_is_pairing_info_irk_not_used(&info->irk)) {
      dev->irk = NULL;
    } else {
      key_irk_t *irk = kernel_zalloc_check(sizeof(key_irk_t));
      memcpy(&irk->key, info->irk.data, sizeof(irk->key));
      dev->irk = irk;
    }
  }

  storage_release();

  PBL_LOG(LOG_LEVEL_DEBUG, "Added pairing for "BT_DEVICE_ADDRESS_FMT,
          BT_DEVICE_ADDRESS_XPLODE(info->identity.address));
}

void bonding_sync_handle_hc_remove(const BleBonding *bonding) {
  const SMPairingInfo *info = &bonding->pairing_info;
  uint16_t conn_idx = BLE_CONN_IDX_INVALID;
  storage_acquire();
  bd_address_t addr;
  dialog_utils_bt_device_to_bd_address(&bonding->pairing_info.identity, &addr);
  device_t *dev = find_device_by_addr(&addr, false /* create */);
  if (dev) {
    conn_idx = dev->conn_idx;
    device_remove_pairing(dev);
  }
  storage_release();

  // set is_gateway to false within the Connection
  if (conn_idx != BLE_CONN_IDX_INVALID) {
    Connection *conn = connection_by_idx(conn_idx);
    if (conn) {
      connection_set_gateway(conn, false);
    }
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Removed pairing for "BT_DEVICE_ADDRESS_FMT,
          BT_DEVICE_ADDRESS_XPLODE(info->identity.address));
}

void bonding_sync_handle_pairing_completed(Connection *connection, uint16_t conn_idx) {
  BleBonding bonding = {};
  SMPairingInfo *info = &bonding.pairing_info;
  bool success = false;

  storage_acquire();
  device_t *dev = find_device_by_conn_idx(conn_idx);
  if (dev && dev->bonded) {
    // The LTK that's used when the local device is the master.
    if (dev->ltk) {
      memcpy(&info->remote_encryption_info.ltk, dev->ltk->key,
             sizeof(info->remote_encryption_info.ltk));
      info->remote_encryption_info.rand = dev->ltk->rand;
      info->remote_encryption_info.ediv = dev->ltk->ediv;
      info->is_remote_encryption_info_valid = true;
    }
    // The LTK that's used when the local device is the slave
    // (we call it "local", Dialog calls it "remote"... :-S )
    if (dev->remote_ltk) {
      memcpy(&info->local_encryption_info.ltk, dev->remote_ltk->key,
             sizeof(info->local_encryption_info.ltk));
      info->local_encryption_info.ediv = dev->remote_ltk->ediv;
      info->local_encryption_info.rand = dev->remote_ltk->rand;
      info->is_local_encryption_info_valid = true;
    }
    if (dev->irk) {
      memcpy(&info->irk, dev->irk->key, sizeof(info->irk));
      dialog_utils_bd_address_to_bt_device(&dev->addr, &info->identity);
      info->is_remote_identity_info_valid = true;
    } else {
      // If the device did not exchange an IRK, we have been given the devices identity address and
      // this is how we will associate our connection with the device in the future. Therefore, mark
      // is_remote_identity_info_valid as true but leave the irk 0'ed out to indicate it is unused
      PBL_LOG(LOG_LEVEL_ALWAYS, "Device did not exchange an IRK");
      info->is_remote_identity_info_valid = true;
      dialog_utils_bd_address_to_bt_device(&dev->addr, &info->identity);
    }
    info->is_mitm_protection_enabled = dev->mitm;
    bonding.is_gateway = connection_is_gateway(connection);

    if (connection_should_pin_address(connection)) {
      bonding.should_pin_address = true;
      connection_get_local_address(connection, &bonding.pinned_address);
    }

    BleBondingFlag flags = 0;

    if (connection_should_auto_accept_re_pairing(connection)) {
      flags |= BleBondingFlag_ShouldAutoAcceptRePairing;
    }

    if (connection_is_reversed_ppogatt_enabled(connection)) {
      flags |= BleBondingFlag_IsReversedPPoGATTEnabled;
    }

    bonding.flags = flags;
    dev->flags = flags;

    success = true;
  } else {
    PBL_LOG(LOG_LEVEL_ERROR, "Pairing fail? conn=%p, dev=%p", connection, dev);
  }
  storage_release();

  if (success) {
    hc_endpoint_bonding_sync_add(&bonding);
  }
}
