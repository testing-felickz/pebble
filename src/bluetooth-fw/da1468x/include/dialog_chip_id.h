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

#include "util/attributes.h"

#include <stdint.h>

typedef enum {
  DialogChipPackageType_WLCSP53 = 0x00,
  DialogChipPackageType_QFN60 = 0xAA,
  DialogChipPackageType_KGD = 0x99,

  DialogChipPackageType_Reserved = 0x55,
} DialogChipPackageType;

//! Timestamp value that corresponds to "1/1/2015 12:00:00 AM", which is a couple months before the
//! the first DA1468x chip was produced. This can be used for sanity checking.
#define DIALOG_CHIP_ID_MIN_TIMESTAMP ((uint32_t)189302400)

typedef struct PACKED {
  //! Info that is programmed by Dialog during the manufacturing process of the chip.
  struct PACKED {
    uint8_t x_coord;
    uint8_t y_coord;
    uint8_t wafer_number;
    DialogChipPackageType package_type:8;
    uint8_t rsvd[4]; // reserved fields, value should == 0
    //! Time in seconds since 1/1/2009 12:00:00 AM
    uint32_t timestamp;
  } info;

  //! ASCII String containing the type of chip, for example "DA14681".
  char chip_id[8];
} DialogChipID;
