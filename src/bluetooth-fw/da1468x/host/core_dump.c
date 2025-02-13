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

#include "core_dump.h"
#include "dialog_spi.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "drivers/flash.h"
#include "kernel/core_dump.h"
#include "kernel/core_dump_private.h"
#include "kernel/pbl_malloc.h"
#include "kernel/util/sleep.h"
#include "services/common/bluetooth/bluetooth_ctl.h"
#include "system/logging.h"
#include "util/attributes.h"
#include "util/build_id.h"
#include "util/crc32.h"
#include "util/math.h"

#include <os/tick.h>

#if PLATFORM_ROBERT
#define CORE_DUMP_RX_BUFFER_SIZE (16 * 1024)
// Place in DTCM on Robert
static unsigned char DMA_BSS DMA_READ_BSS s_rx_buffer[CORE_DUMP_RX_BUFFER_SIZE];
#else
#define CORE_DUMP_RX_BUFFER_SIZE (1 * 1024)
static unsigned char *s_rx_buffer;
#endif
static uint32_t DMA_BSS DMA_READ_BSS s_rx_crc;

static SemaphoreHandle_t s_spi_dma_semph;
static SemaphoreHandle_t s_spi_int_semph;


static void prv_handshake_int_exti_cb(bool *should_context_switch) {
  // Ignore Int -- not needed during initial handshaking
  *should_context_switch = false;
}


static bool prv_handshake(void) {
  uint8_t buffer[sizeof(core_dump_connect_response)];

  PBL_LOG(LOG_LEVEL_ERROR, "BLE Hang/Crash ... attempt to connect");
  // Attempt to handshake with the controller
  for (int count = 0; count < 100; ++count) {

    psleep(10);
    dialog_spi_transmit_data_no_wait((uint8_t *)core_dump_connect_ping,
                                      sizeof(core_dump_connect_ping));
    psleep(10);
    dialog_spi_receive_data_no_wait(buffer, sizeof(buffer));
    if (memcmp(buffer, core_dump_connect_response, sizeof(core_dump_connect_response)) == 0) {
      PBL_LOG(LOG_LEVEL_INFO, "Core Dump connect response received");
      return true;
    }
  }
  PBL_LOG(LOG_LEVEL_ERROR, "Failed to connect with BLE chip to collect coredump");
  return false;
}

static void prv_int_exti_cb(bool *should_context_switch) {
  xSemaphoreGiveFromISR(s_spi_int_semph, NULL);
}

static void prv_dma_cb(bool *should_context_switch) {
  portBASE_TYPE was_higher_task_woken = false;
  xSemaphoreGiveFromISR(s_spi_dma_semph, &was_higher_task_woken);
  *should_context_switch = (was_higher_task_woken != false);
}

static bool prv_spi_send_and_receive_dma(
    const uint8_t *tx_buffer, uint8_t *rx_buffer, size_t length) {
  // arbitrary timeout that should give us plenty of time to receieve a response within
  const uint32_t tick_timeout = milliseconds_to_ticks(6000);

  bool success = false;
  if (xSemaphoreTake(s_spi_int_semph, tick_timeout) == pdTRUE) {
    dialog_spi_send_and_receive_dma(tx_buffer, rx_buffer, length, prv_dma_cb);
    success = (xSemaphoreTake(s_spi_dma_semph, tick_timeout) == pdTRUE);
  }

  return success;
}

static bool prv_spi_transaction(CoreDumpSPICmd *cmd, uint8_t *buffer, size_t length) {
  // The protocol works thustly: 1) wait for EXTI INT, which the controller asserts when it's ready
  // to receive. 2) Send/receive data. 3) Wait for DMA to complete.
  // The overall transaction is: 1) send cmd 2) receive data, 3) receive CRC, 4) confirm CRC.

  cmd->crc32 = crc32(CRC32_INIT, cmd, sizeof(CoreDumpSPICmd) - sizeof(cmd->crc32));

  if (!prv_spi_send_and_receive_dma((uint8_t *)cmd, NULL, sizeof(CoreDumpSPICmd))) {
    return false;
  }

  if (length) {
    if (!prv_spi_send_and_receive_dma(NULL, buffer, length) ||
        !prv_spi_send_and_receive_dma(NULL, (uint8_t *)&s_rx_crc, sizeof(uint32_t))) {
      return false;
    }

    uint32_t crc = crc32(CRC32_INIT, buffer, length);
    if (crc != s_rx_crc) {
      return false; // TODO: At least one memory region will crc fail because of changing counter
      // variables in the crc32/SPI code. We'll need to move the core dump code to its own region.
    }
  }

  return true;
}

static void prv_core_dump_main(void) {
  CoreDumpSPICmd cmd;
  CoreDumpChunkHeader chunk_hdr;

  uint16_t text_crc_matches;  // This can't be a bool (1-byte). The Dialog SPI/DMA Tx will break.
  uint8_t build_id[BUILD_ID_TOTAL_EXPECTED_LEN];

#if !PLATFORM_ROBERT
  s_rx_buffer = kernel_malloc_check(CORE_DUMP_RX_BUFFER_SIZE);
#endif
  const size_t region_buf_size = sizeof(MemoryRegion[MemoryRegionTagCount]);
  MemoryRegion *region_buf = (MemoryRegion *)kernel_malloc_check(region_buf_size);

  // Does the .text section crc match?
  cmd = (CoreDumpSPICmd) { .cmd = CoreDumpCmd_GetTextCRC };
  if (!prv_spi_transaction(&cmd, s_rx_buffer, sizeof(text_crc_matches))) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to get Text CRC");
    goto spi_failed;
  }
  memcpy(&text_crc_matches, s_rx_buffer, sizeof(text_crc_matches));

  // Get the Build ID
  cmd = (CoreDumpSPICmd) { .cmd = CoreDumpCmd_GetBuildID };
  if (!prv_spi_transaction(&cmd, s_rx_buffer, sizeof(build_id))) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to get Build ID");
    goto spi_failed;
  }
  memcpy(build_id, s_rx_buffer, sizeof(build_id));

  // Get the memory region table
  cmd = (CoreDumpSPICmd) { .cmd = CoreDumpCmd_ReadRegionTable };
  if (!prv_spi_transaction(&cmd, s_rx_buffer, region_buf_size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to get Region Table");
    goto spi_failed;
  }
  memcpy(region_buf, s_rx_buffer, region_buf_size);

  // Dump the memory region table
  for (int index = 0; index < MemoryRegionTagCount; ++index) {
    PBL_LOG(LOG_LEVEL_ERROR, "Memory Region %d: %p - 0x%lx", index, region_buf[index].start,
            region_buf[index].length);
  }

  // Get a core dump flash memory slot
  uint32_t flash_addr, max_size;
  if (!core_dump_reserve_ble_slot(&flash_addr, &max_size, (ElfExternalNote *)build_id)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Can't reserve slot for BLE core dump");
    goto powerdown;
  }
  uint32_t flash_addr_max = flash_addr + max_size;
  PBL_LOG(LOG_LEVEL_DEBUG, "Using flash slot %lx %lx", flash_addr, flash_addr_max);

  // Read every region from the region table in blocks of 16k.
  cmd.cmd = CoreDumpCmd_ReadMemory;
  for (int region = 0; region < MemoryRegionTagCount; ++region) {
    if (region == MemoryRegionTag_Text && text_crc_matches) {
      continue;
    }

    // Write out a chunk header
    chunk_hdr.key = CORE_DUMP_CHUNK_KEY_MEMORY;
    chunk_hdr.size = region_buf[region].length + sizeof(CoreDumpMemoryHeader);
    if (flash_addr_max < (flash_addr + sizeof(chunk_hdr) + chunk_hdr.size)) {
      PBL_LOG(LOG_LEVEL_ERROR, "Insufficient space for BLE core dump -- RAM");
      goto powerdown;
    }
    flash_write_bytes((const uint8_t *)&chunk_hdr, flash_addr, sizeof(chunk_hdr));
    flash_addr += sizeof(chunk_hdr);

    // Write out a memory chunk header
    CoreDumpMemoryHeader mem_hdr;
    mem_hdr.start = (uint32_t)region_buf[region].start;
    flash_write_bytes((const uint8_t *)&mem_hdr, flash_addr, sizeof(mem_hdr));
    flash_addr += sizeof(mem_hdr);

    size_t rx_len_remaining = region_buf[region].length;
    uint32_t curr_rx_offset = 0;
    while (rx_len_remaining) {
      const size_t rx_len = MIN(rx_len_remaining, CORE_DUMP_RX_BUFFER_SIZE);

      cmd.len = rx_len;
      cmd.addr = (uint8_t *)region_buf[region].start + curr_rx_offset;
      if (!prv_spi_transaction(&cmd, s_rx_buffer, rx_len)) {
        // TODO goto spi_failed; -- don't download the active controller SPI loop variables!
      }

      flash_write_bytes(s_rx_buffer, flash_addr, rx_len);
      flash_addr += rx_len;

      curr_rx_offset += rx_len;
      rx_len_remaining -= rx_len;
    }
  }

  // Read the Thread Info
  chunk_hdr.key = CORE_DUMP_CHUNK_KEY_THREAD;
  chunk_hdr.size = sizeof(CoreDumpThreadInfo);
  if (flash_addr_max < (flash_addr + sizeof(chunk_hdr) + chunk_hdr.size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Insufficient space for BLE core dump -- TIB");
    goto powerdown;
  }
  cmd.cmd = CoreDumpCmd_ReadRunningThreadInfo;
  if (!prv_spi_transaction(&cmd, s_rx_buffer, sizeof(CoreDumpThreadInfo))) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to get Thread Info");
    goto spi_failed;
  }
  flash_write_bytes((const uint8_t *)&chunk_hdr, flash_addr, sizeof(chunk_hdr));
  flash_addr += sizeof(chunk_hdr);
  flash_write_bytes(s_rx_buffer, flash_addr, sizeof(CoreDumpThreadInfo));
  flash_addr += sizeof(CoreDumpThreadInfo);

  // Read the Extra Thread Info
  CoreDumpExtraRegInfo chunk_extra;

  chunk_hdr.key = CORE_DUMP_CHUNK_KEY_EXTRA_REG;
  chunk_hdr.size = sizeof(chunk_extra);
  if (flash_addr_max < (flash_addr + sizeof(chunk_hdr) + chunk_hdr.size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Insufficient space for BLE core dump -- ExtraTI");
    goto powerdown;
  }
  cmd.cmd = CoreDumpCmd_ReadExtraThreadInfo;
  if (!prv_spi_transaction(&cmd, s_rx_buffer, sizeof(CoreDumpExtraRegInfo))) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to get Extra Thread Info");
    goto spi_failed;
  }
  flash_write_bytes((const uint8_t *)&chunk_hdr, flash_addr, sizeof(chunk_hdr));
  flash_addr += sizeof(chunk_hdr);
  flash_write_bytes(s_rx_buffer, flash_addr, sizeof(CoreDumpExtraRegInfo));
  flash_addr += sizeof(CoreDumpExtraRegInfo);

  // Terminate the core dump
  chunk_hdr.key = CORE_DUMP_CHUNK_KEY_TERMINATOR;
  chunk_hdr.size = 0;
  if (flash_addr_max < (flash_addr + sizeof(chunk_hdr) + chunk_hdr.size)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Insufficient space for BLE core dump terminator");
    goto powerdown;
  }
  flash_write_bytes((const uint8_t *)&chunk_hdr, flash_addr, sizeof(chunk_hdr));

powerdown:
  // Power down the chip
  cmd = (CoreDumpSPICmd) { .cmd = CoreDumpCmd_LowPowerMode };
  if (!prv_spi_transaction(&cmd, NULL, 0)) {
    PBL_LOG(LOG_LEVEL_ERROR, "Unable to power down");
    goto spi_failed;
  }

spi_failed:
#if !PLATFORM_ROBERT
  kernel_free(s_rx_buffer);
#endif
  kernel_free(region_buf);

  PBL_LOG(LOG_LEVEL_INFO, "BLE Core Dump complete");
}

// NB: this function runs on the host_transport thread (BTTrans).
static void prv_core_dump(void) {
  // Disable host-transport's use of SPI & interrupts
  dialog_spi_deinit();

  s_spi_dma_semph = xSemaphoreCreateBinary();
  s_spi_int_semph = xSemaphoreCreateBinary();

  // Enable SPI. Use our own interrupt handler.
  dialog_spi_init(prv_handshake_int_exti_cb);
  if (prv_handshake()) {
    dialog_spi_deinit();
    dialog_spi_init(prv_int_exti_cb);
    prv_core_dump_main();
  }
  dialog_spi_deinit();

  vSemaphoreDelete(s_spi_dma_semph);
  vSemaphoreDelete(s_spi_int_semph);
}

void core_dump_and_reset_or_reboot(void) {
      prv_core_dump();
#if REBOOT_ON_BT_CRASH
      RebootReason reboot_reason = {
        .code = RebootReasonCode_BtCoredump,
      };

      reboot_reason_set(&reboot_reason);

      // Just reset the board, we don't want to wind up adding a normal coredump for the MCU
      system_hard_reset();
#else
      // Core dump has completed. Trigger a stack reset.
      bt_ctl_reset_bluetooth();
#endif
}
