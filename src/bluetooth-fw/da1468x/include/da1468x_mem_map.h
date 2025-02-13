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

#define DATA_RAM_BASE_ADDRESS 0x7FC0000
#define DATA_RAM_SIZE (128 * 1024)

#define CACHE_RAM_BASE_ADDRESS 0x7FE0000
#define CACHE_RAM_SIZE (16 * 1024)

#define OTP_POS_PKG_TIMESTAMP_ADDRESS   (0x7F8EA00)
#define OTP_CHIP_ID_ADDRESS             (0x7F8EA20)

// Copy pasta from ble_stack_config.h
#if ((dg_configBLACK_ORCA_IC_REV == BLACK_ORCA_IC_REV_A) \
     && (dg_configBLACK_ORCA_IC_STEP <= BLACK_ORCA_IC_STEP_D))
#  define BLE_VAR_ADDR            (0x7FDEC00)
#elif ((dg_configBLACK_ORCA_IC_REV == BLACK_ORCA_IC_REV_A) \
       && (dg_configBLACK_ORCA_IC_STEP == BLACK_ORCA_IC_STEP_E))
#  define BLE_VAR_ADDR            (0x7FDC000)
#else
#  error "Unsupported chip version"
#endif
