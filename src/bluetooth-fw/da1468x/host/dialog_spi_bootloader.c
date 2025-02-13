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

#include <stdint.h>
#include <string.h>

#include "dialog_spi.h"

#include "kernel/pbl_malloc.h"
#include "resource/resource.h"
#include "resource/resource_ids.auto.h"
#include "system/logging.h"
#include "util/attributes.h"
#include "util/crc32.h"
#include "util/math.h"
#include "util/units.h"

//
// SPI bootloader protocol
//

static bool prv_load_data(uint32_t address, const uint8_t *data, size_t data_len) {
  // PBL_LOG(LOG_LEVEL_DEBUG, "Loading %d bytes start at 0x%x", (int)data_len, (int)address);

  uint8_t mem_write[] = {'L', 'D'};
  dialog_spi_transmit_data(mem_write, sizeof(mem_write));

  struct __attribute__((packed)) {
    uint32_t copy_address;
    uint32_t length;
  } send_data_cmd_payload = {
    .copy_address = address,
    .length = data_len
  };

  dialog_spi_transmit_data((uint8_t *)&send_data_cmd_payload, sizeof(send_data_cmd_payload));

  dialog_spi_wait_for_int();
  dialog_spi_start_cmd();
  uint32_t expected_crc = CRC32_INIT;
  for (size_t i = 0; i < send_data_cmd_payload.length; i++) {
    // This could be made faster by using DMA if we needed
    expected_crc = crc32(expected_crc, &data[i], 1);
    dialog_spi_send_and_receive_byte(data[i]);
  }
  dialog_spi_end_cmd();

  uint32_t received_crc = 0;
  dialog_spi_receive_data((uint8_t *)&received_crc, sizeof(received_crc));

  if (received_crc != expected_crc) {
    PBL_LOG(LOG_LEVEL_ERROR, "Load Data Crc. Expected: 0x%x Received CRC: 0x%x",
            (int)expected_crc, (int)received_crc);
  }
  return (received_crc == expected_crc);
}

static bool prv_send_vt(const uint8_t *vt, uint32_t vt_size_bytes) {
  uint8_t vt_write[] = {'V', 'T'};
  dialog_spi_transmit_data(vt_write, sizeof(vt_write));

  uint8_t entries = vt_size_bytes / 4;
  dialog_spi_transmit_data((uint8_t *)&entries, sizeof(entries));

  dialog_spi_wait_for_int();
  dialog_spi_start_cmd();
  uint32_t expected_crc = CRC32_INIT;
  for (size_t i = 0; i < vt_size_bytes; i++) {
    // This could be made faster by using DMA if we needed
    expected_crc = crc32(expected_crc, &vt[i], 1);
    dialog_spi_send_and_receive_byte(vt[i]);
  }
  dialog_spi_end_cmd();

  uint32_t received_crc = 0;
  dialog_spi_receive_data((uint8_t *)&received_crc, sizeof(received_crc));

  PBL_LOG(LOG_LEVEL_DEBUG, "VT Crc. Expected: 0x%x Received CRC: 0x%x",
          (int)expected_crc, (int)received_crc);
  return (received_crc == expected_crc);
}

static bool prv_send_hi_cmd(void) {
  uint8_t send[] = {'H', 'I'};
  dialog_spi_transmit_data(send, sizeof(send));

  uint8_t response[4] = {};
  dialog_spi_receive_data(response, sizeof(response));

  uint8_t expected_response[] = {'H', 'E', 'R', 'E'};

  if (memcmp(expected_response, response, sizeof(response)) == 0) {
    return true;
  }

  PBL_LOG(LOG_LEVEL_WARNING, "Got unexpected response, { 0x%x 0x%x 0x%x 0x%x }",
          (int)response[0], (int)response[1], (int)response[2], (int)response[3]);

  return false;
}

// This struct is packed at the beginning of the binary blob
// See main/ldscripts/mem.ld.h for more info
typedef struct PACKED {
  uint32_t load_start_address;
  uint32_t vector_table_size;
} ObjectBinInfo;

static bool prv_load_and_transfer_vector_table(uint32_t vector_table_size) {
  bool success = false;

  uint8_t *vt_buf = kernel_malloc_check(vector_table_size);
  if (!resource_load_byte_range_system(
          SYSTEM_APP, RESOURCE_ID_BT_FW_IMAGE, sizeof(ObjectBinInfo),
          vt_buf, vector_table_size)) {
    goto cleanup;
  }

  if (!prv_send_vt(vt_buf, vector_table_size)) {
    PBL_LOG(LOG_LEVEL_WARNING, "Failed to send VT!");
    goto cleanup;
  }

  success = true;
cleanup:
  kernel_free(vt_buf);
  return success;
}

static bool prv_transfer_main_image_code_and_data(
    uint32_t image_size, ObjectBinInfo *info) {
  bool success = false;

  int vt_and_info_blob_size = info->vector_table_size + sizeof(ObjectBinInfo);
  image_size -= vt_and_info_blob_size;

  const uint32_t data_buf_size = 4096;
  uint8_t *data_buf = kernel_malloc_check(data_buf_size);

  uint32_t curr_send_offset = 0;
  while (image_size != 0) {
    const size_t send_len = MIN(image_size, data_buf_size);

    if (!resource_load_byte_range_system(
            SYSTEM_APP, RESOURCE_ID_BT_FW_IMAGE,
            curr_send_offset + vt_and_info_blob_size,
            data_buf, send_len)) {
      PBL_LOG(LOG_LEVEL_WARNING, "Failed to read fw image data");
      goto cleanup;
    }

    if (!prv_load_data(info->load_start_address + curr_send_offset, data_buf,
                       send_len)) {
      PBL_LOG(LOG_LEVEL_WARNING, "SPI transfer to BLE chip not successful");
      goto cleanup;
    }

    curr_send_offset += send_len;
    image_size -= send_len;
  }

  success = true;

cleanup:
  kernel_free(data_buf);
  return success;
}

static bool prv_transfer_main_image(void) {
  // TODO: Would be nice to just leverage memory mapping to avoid the double
  // copy in the future

  // Get the basic info about the image we are going to transfer
  uint32_t image_size = resource_size(SYSTEM_APP, RESOURCE_ID_BT_FW_IMAGE);
  ObjectBinInfo info;
  if (!resource_load_byte_range_system(SYSTEM_APP, RESOURCE_ID_BT_FW_IMAGE,
                                       0x0, (uint8_t *)&info, sizeof(info))) {
    PBL_LOG(LOG_LEVEL_WARNING, "Failed to receive object bin info");
    return false;
  }

  PBL_LOG(LOG_LEVEL_INFO, "Loading BT FW image to 0x%x. VT Size: %"PRIu32,
          (int)info.load_start_address, info.vector_table_size);

  if (!prv_load_and_transfer_vector_table(info.vector_table_size)) {
    return false;
  }

  if (!prv_transfer_main_image_code_and_data(image_size, &info)) {
    return false;
  }

  return true;
}

static void prv_issue_bt_reboot(void) {
  uint8_t reboot[] = {'R', 'T'};
  dialog_spi_transmit_data(reboot, sizeof(reboot));
}

static bool prv_load_bt_image_over_spi(void) {
  PBL_LOG(LOG_LEVEL_DEBUG, "Bringing up the BT Stack");

  if (!prv_send_hi_cmd()) {
    PBL_LOG(LOG_LEVEL_WARNING, "Failed to load firmware onto BLE Chip");
    return false; // TODO: This should probably eventually insta-PRF
  }

  if (!prv_transfer_main_image()) {
    return false;
  }

  prv_issue_bt_reboot();

  PBL_LOG(LOG_LEVEL_INFO, "BLE FW loaded!");
  return true;
}

//
// Exported routines
//

bool dialog_spi_bootloader_load_image(void) {
  dialog_spi_init(NULL);
  const bool result = prv_load_bt_image_over_spi();
  dialog_spi_deinit();

  return result;
}

void dialog_test_cmds(void) {
  dialog_spi_bootloader_load_image();
}
