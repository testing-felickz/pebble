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

#include "gatt_client_discovery.h"

#include "connection.h"
#include "gatt_wrapper.h"
#include "hc_protocol/hc_endpoint_discovery.h"
#include "kernel/pbl_malloc.h"
#include "ppogatt_emulated_server_wa.h"
#include "system/logging.h"
#include "system/passert.h"
#include "util/uuid.h"

// Dialog Headers
#include "ble_gatt.h"
#include "ble_gattc.h"
#include "rwble_hl_error.h"
#include "ble_uuid.h"

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/gatt_service_types.h>
#include <bluetooth/pebble_bt.h>
#include <util/uuid.h>

#define GATT_SERVICE_UUID ((uint16_t) 0x1801)
#define GATT_SERVICE_CHANGED_CHARACTERISTIC_UUID ((uint16_t) 0x2A05)
#define GATT_CCCD_UUID ((uint16_t) 0x2902)

// Converts from a Dialog 'att_uuid_t' to firmware 'uuid' representation
static void prv_att_uuid_to_uuid(const att_uuid_t *att_uuid, Uuid *uuid) {
  if (att_uuid->type == ATT_UUID_16) {
    uint16_t uuid16 = att_uuid->uuid16;
    *uuid = (Uuid){ BT_UUID_EXPAND(uuid16) };
  } else if (att_uuid->type == ATT_UUID_128) {
    *uuid = UuidMakeFromLEBytes(att_uuid->uuid128);
  } else { // should never happen
    PBL_ASSERTN(0);
  }
}

T_STATIC HcProtocolDiscoveryServiceFoundPayload *prv_gatt_client_discovery_build_gatt_service(
    const ble_evt_gattc_browse_svc_t *svc, uint32_t *payload_size) {
  uint8_t type_counts[GATTC_ITEM_NUM_TYPES] = { };
  for (int i = 0; i < svc->num_items; i++) {
    const gattc_item_t *item = &svc->items[i];
    if (item->type >= GATTC_ITEM_NUM_TYPES) {
      PBL_LOG(LOG_LEVEL_ERROR, "Unexpected gattc type: 0x%x!", (int)item->type);
    }
    type_counts[item->type]++;
  }

  uint32_t blob_size = COMPUTE_GATTSERVICE_SIZE_BYTES(
      type_counts[GATTC_ITEM_TYPE_CHARACTERISTIC], type_counts[GATTC_ITEM_TYPE_DESCRIPTOR],
      type_counts[GATTC_ITEM_TYPE_INCLUDE]);

  uint32_t payload_extra_size =
      sizeof(HcProtocolDiscoveryServiceFoundPayload) - sizeof(GATTService);
  *payload_size = payload_extra_size + blob_size;
  HcProtocolDiscoveryServiceFoundPayload *payload = kernel_zalloc_check(*payload_size);

  GATTService *service = &payload->service;

  uint16_t base_handle = svc->start_h;

  *service = (GATTService) {
    .size_bytes = blob_size,
    .att_handle = base_handle,
    .num_characteristics = type_counts[GATTC_ITEM_TYPE_CHARACTERISTIC],
    .num_descriptors = type_counts[GATTC_ITEM_TYPE_DESCRIPTOR],
    .num_att_handles_included_services = type_counts[GATTC_ITEM_TYPE_INCLUDE],
  };

  prv_att_uuid_to_uuid(&svc->uuid, &service->uuid);

  char uuid_str[UUID_STRING_BUFFER_LENGTH];
  uuid_to_string(&service->uuid, uuid_str);
  PBL_LOG(LOG_LEVEL_DEBUG,
          "Found Service %s Handle: 0x%"PRIx16": 0x%"PRIx16" (%"PRIu32" byte blob)",
          uuid_str, base_handle, svc->end_h, blob_size);

  uint16_t *att_handles_included_services =
      (uint16_t *)((uint8_t *)service + blob_size -
                   sizeof(uint16_t) * type_counts[GATTC_ITEM_TYPE_INCLUDE]);
  uint16_t att_idx = 0;

  GATTCharacteristic *char_dest = NULL;
  uint8_t *end_ptr = (uint8_t *)service->characteristics;

  // A few notes:
  //  + We expect the descriptors to be part of the last discovered characteristic
  //  + We assume Includes can show up anywhere
  //  + If we find descriptors before any characteristic is found, we bail (it shouldn't happen)
  for (int i = 0; i < svc->num_items; i++) {
    const gattc_item_t *item = &svc->items[i];
    if (item->type == GATTC_ITEM_TYPE_CHARACTERISTIC) {
      char_dest = (GATTCharacteristic *)end_ptr;
      *char_dest = (GATTCharacteristic) {
        .att_handle_offset = (item->c.value_handle - base_handle),
        .properties = item->c.properties,
      };
      prv_att_uuid_to_uuid(&item->uuid, &char_dest->uuid);
      end_ptr += sizeof(GATTCharacteristic);

      uuid_to_string(&char_dest->uuid, uuid_str);
      PBL_LOG(LOG_LEVEL_DEBUG,
              "  Found Characteristic %s Handle 0x%"PRIx16, uuid_str, item->c.value_handle);
    } else if (item->type == GATTC_ITEM_TYPE_DESCRIPTOR) {
      if (char_dest == NULL) {
        PBL_LOG(LOG_LEVEL_ERROR, "No characteristic found for descriptor! handle=0x%x",
                (int)item->handle);
        goto failure;
      }

      GATTDescriptor *desc_dest = (GATTDescriptor *)end_ptr;
      *desc_dest = (GATTDescriptor) {
        .att_handle_offset = item->handle - base_handle,
      };
      prv_att_uuid_to_uuid(&item->uuid, &desc_dest->uuid);
      char_dest->num_descriptors++;
      end_ptr += sizeof(GATTDescriptor);

      uuid_to_string(&desc_dest->uuid, uuid_str);
      PBL_LOG(LOG_LEVEL_DEBUG,
              "    Found Descriptor %s Handle 0x%x", uuid_str, item->handle);

    } else if (item->type == GATTC_ITEM_TYPE_INCLUDE) {
      att_handles_included_services[att_idx] = item->i.start_h;
      att_idx++;

      PBL_LOG(LOG_LEVEL_DEBUG, "Found Include, Start Handle 0x%x", item->i.start_h);
    }
  }

  return payload;
failure:
  kernel_free(payload);
  return NULL;
}

static void prv_write_cccd_cb(const ble_evt_gattc_write_completed_t *evt) {
  if (evt->status != ATT_ERROR_OK) {
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to write CCCD for idx %d Handle 0x%04X",
            evt->conn_idx, evt->handle);
  }
}

//! Inspects the GATT service discovery event in search for the GATT Profile Service and its
//! Service Changed characteristic. In case it's found, its ATT handle is recorded in the
//! GAPLEConnection structure (by passing it up to the module. The characteristic's indications
//! are subscribed to as well.
static void prv_search_service_changed_handle(Connection *connection,
                                              const ble_evt_gattc_browse_svc_t *evt) {

  // Check whether the service is the "GATT Profile Service":
  att_uuid_t gatt_service_uuid;
  ble_uuid_create16(GATT_SERVICE_UUID, &gatt_service_uuid);

  att_uuid_t service_changed_characteristic_uuid;
  ble_uuid_create16(GATT_SERVICE_CHANGED_CHARACTERISTIC_UUID, &service_changed_characteristic_uuid);

  // Attempt to find the "Service Changed" characteristic:
  for (uint16_t i = 0; i < evt->num_items; ++i) {
    const gattc_item_t *characteristic_info = &evt->items[i];

    // Sanity: make sure this is a characteristic
    if (characteristic_info->type != GATTC_ITEM_TYPE_CHARACTERISTIC) {
      continue;
    }

    if (!ble_uuid_equal(&characteristic_info->uuid, &service_changed_characteristic_uuid)) {
      continue;
    }

    // Found the Service Changed characteristic!
    att_uuid_t cccd_uuid;
    ble_uuid_create16(GATT_CCCD_UUID, &cccd_uuid);
    bool cccd_found = false;

    // Attempt to find the CCCD:
    for (uint16_t j = 0; j < evt->num_items; ++j) {
      const gattc_item_t *descriptor_info = &evt->items[j];

      // Sanity: make sure this is a descriptor
      if (descriptor_info->type != GATTC_ITEM_TYPE_DESCRIPTOR) {
        continue;
      }

      if (!ble_uuid_equal(&cccd_uuid, &descriptor_info->uuid)) {
        continue;
      }

      // Found the CCCD!

      // ... and finally subscribe to indications:
      const uint16_t cccd_value = GATT_CCC_INDICATIONS;
      ble_error_t result = gatt_wrapper_write(evt->conn_idx, descriptor_info->handle,
                                              sizeof(cccd_value), (const uint8_t *)&cccd_value,
                                              (uintptr_t)prv_write_cccd_cb,
                                              GattReqSourceController);
      if (result != BLE_STATUS_OK) {
        PBL_LOG(LOG_LEVEL_ERROR, "Failed to write CCCD %d", result);
      }
      cccd_found = true;
      break;
    }

    if (!cccd_found) {
      // gah, Android doesn't seem to create the CCCD, but it does seem to auto-subscribe any
      // bonded device so let's assume this is what happen and notify the main firmware that we are
      // subscribed.
      PBL_LOG(LOG_LEVEL_DEBUG, "No cccd found for service changed characteristic, assuming we are "
              "auto-subscribed");
    }

    // We've got everything we need, record the characteristic value handle, so we can filter
    // out the Service Changed indications later on:
    BTDeviceInternal address;
    connection_get_address(connection, &address);

    hc_endpoint_discovery_service_changed_handle(&address, characteristic_info->c.value_handle);
  }
}

void gatt_client_discovery_process_service(const ble_evt_gattc_browse_svc_t *service) {
  uint32_t payload_size;
  HcProtocolDiscoveryServiceFoundPayload *payload =
      prv_gatt_client_discovery_build_gatt_service(service, &payload_size);
  if (!payload) {
    return;
  }
  Connection *connection = connection_by_idx_check(service->conn_idx);
  connection_get_address(connection, &payload->address);
  hc_endpoint_discovery_send_service_found(payload, payload_size);

  const Uuid ppogatt_uuid =
      (const Uuid){PEBBLE_BT_UUID_EXPAND(PEBBLE_BT_PPOGATT_SERVICE_UUID_32BIT)};
  if (uuid_equal(&payload->service.uuid, &ppogatt_uuid)) {
    PBL_LOG(LOG_LEVEL_DEBUG, "PPoGATT found");
    ppogatt_emulated_notify_phone_ppogatt_server_found(connection);
  }

  kernel_free(payload);

  prv_search_service_changed_handle(connection, service);
}

void gatt_client_discovery_handle_complete(const ble_evt_gattc_browse_completed_t *complete_event) {
  ppogatt_inject_emulated_ppogatt_service_if_needed(complete_event->conn_idx);

  PBL_LOG(LOG_LEVEL_DEBUG, "Gatt Service Discovery Complete: %d", complete_event->status);
  BTDeviceInternal addr;
  Connection *connection = connection_by_idx_check(complete_event->conn_idx);

  connection_get_address(connection, &addr);
  HciStatusCode status = HciStatusCode_Success;
  if (complete_event->status !=  GAP_ERR_NO_ERROR) {
    if (complete_event->status == GAP_ERR_INSUFF_RESOURCES) {
      // Due to a bug in the Dialog ROM (PBL-35827) we may get this error. We
      // assume all the discovery of services up to this point was successful,
      // so return "Success" to the Host
      PBL_LOG(LOG_LEVEL_WARNING, "GATT discovery failed due to OOM, "
              "considering discovery up to last handle a success");
    } else {
      PBL_LOG(LOG_LEVEL_ERROR, "GATT discovery failed, %d\n", complete_event->status);
      status = HciStatusCode_VS_Base + complete_event->status;
    }
  }

  hc_endpoint_discovery_complete(&addr, status);
}
