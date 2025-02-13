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

#include "pebbleos/firmware_metadata.h"

// Include generated files from the FW build. This should be fixed in WAF.
#include "../../src/fw/git_version.auto.h"

const FirmwareMetadata TINTIN_METADATA SECTION(".pbl_fw_version") = {
  .version_timestamp = GIT_TIMESTAMP,
  .version_tag = GIT_TAG,
  .version_short = GIT_REVISION,
  .is_recovery_firmware = FIRMWARE_METADATA_IS_RECOVERY_FIRMWARE,
  .is_ble_firmware = true,
  .reserved = 0,
  .hw_platform = FIRMWARE_METADATA_HW_PLATFORM,
  .metadata_version = FW_METADATA_CURRENT_STRUCT_VERSION,
};
