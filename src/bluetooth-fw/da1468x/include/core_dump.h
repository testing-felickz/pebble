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

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "pebbleos/core_dump_structs.h"
#include "util/attributes.h"

// Host/Controller handshake
static const uint8_t core_dump_connect_ping[] = { 0x5A, 0x67, 0x57, 0xBC, 0x59 };
static const uint8_t core_dump_connect_response[] = { 0xA5, 0xEA, 0xB8, 0xBE, 0x74 };

typedef enum MemoryRegionTag {
  MemoryRegionTag_Text,
  MemoryRegionTag_VectorTable,
  MemoryRegionTag_Heap,
  MemoryRegionTag_RwData,
  MemoryRegionTag_StackAndBss,
  MemoryRegionTag_BleVariables,
  MemoryRegionTag_CacheRam,
  MemoryRegionTag_RebootReason,
  MemoryRegionTagCount
} MemoryRegionTag;

typedef struct MemoryRegion {
  MemoryRegionTag tag;
  const void *start;
  const uint32_t length;
} MemoryRegion;

typedef enum {
  CoreDumpCmd_GetTextCRC = 0x01,
  CoreDumpCmd_GetBuildID = 0x02,
  CoreDumpCmd_ReadRegionTable = 0x03,
  CoreDumpCmd_ReadMemory = 0x04,
  CoreDumpCmd_ReadRunningThreadInfo = 0x05,
  CoreDumpCmd_ReadExtraThreadInfo = 0x06,
  CoreDumpCmd_LowPowerMode = 0x07,
} CoreDumpCmd_Cmds;

typedef struct PACKED CoreDumpSPICmd {
  uint8_t cmd;
  uint16_t len;
  void * addr;
  uint32_t crc32;
} CoreDumpSPICmd;

// Controller functions
NORETURN core_dump(bool user_requested);

// Host functions
void core_dump_and_reset_or_reboot(void);

// Fault Handler Stack Frame Contains:
// r0, r1, r2, r3, r12, r14, the return address and xPSR
// - Stacked R0 = hf_args[0]
// - Stacked R1 = hf_args[1]
// - Stacked R2 = hf_args[2]
// - Stacked R3 = hf_args[3]
// - Stacked R12 = hf_args[4]
// - Stacked LR = hf_args[5]
// - Stacked PC = hf_args[6]
// - Stacked xPSR= hf_args[7]

typedef enum {
  Stacked_Register_R0 = 0,
  Stacked_Register_R1,
  Stacked_Register_R2,
  Stacked_Register_R3,
  Stacked_Register_R12,
  Stacked_Register_LR,
  Stacked_Register_PC,
  Stacked_Register_xPSR,
  Stacked_Register_Count,
} Stacked_Register;

static const uint16_t stacked_regs_size = (Stacked_Register_Count * sizeof(uint32_t));
