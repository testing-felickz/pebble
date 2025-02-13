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

#include <bluetooth/dis.h>

#include "dis_impl.h"
#include "system/logging.h"
#include "system/passert.h"

// Dialog headers
#include "dis.h"

static DisInfo s_dis_info;

void device_information_service_init(const DisInfo *dis_info) {
  s_dis_info = *dis_info;
}

bool device_information_service_register(uint16_t start_hdl) {
  dis_device_info_t device_info = {
    .model_number = s_dis_info.model_number,
    .manufacturer = s_dis_info.manufacturer,
    .serial_number = s_dis_info.serial_number,
    .fw_revision = s_dis_info.fw_revision,
    .sw_revision = s_dis_info.sw_revision,
  };

  bool rv = true;
  ble_service_t *ble_service = dis_init(NULL, &device_info, start_hdl);
  if (!ble_service) {
    PBL_LOG(LOG_LEVEL_ERROR, "DIS failed to init!");
    rv = false;
  } else {
    PBL_ASSERTN(start_hdl == ble_service->start_h);
  }
  return rv;
}
