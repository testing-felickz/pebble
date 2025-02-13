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

#include <bluetooth/init.h>
#include <bluetooth/temp.h>

#include "dialog_bootrom.h"
#include "dialog_spi_bootloader.h"
#include "hc_protocol/hc_endpoint_ctl.h"
#include "hc_protocol/hc_protocol.h"
#include "services/common/bluetooth/local_id.h"

#include "host_transport_impl.h"

// FW Includes:
#include "comm/bt_lock.h"
#include "kernel/events.h"
#include "kernel/util/standby.h"
#include "pebble_errors.h"
#include "resource/system_resource.h"
#include "system/logging.h"
#include "system/passert.h"
#include "system/reboot_reason.h"

void bt_driver_init(void) {
  // Perform any one-time initialization here that should happen when the main FW boots.
  bt_lock_init();
  hc_protocol_boot();
}

bool bt_driver_start(BTDriverConfig *config) {
  if (!system_resource_is_valid()) {
    return false;
  }
  // Do stuff that needs to happen when stack starts, i.e. bootstrap, load FW, ...

  if (!dialog_bootrom_load_second_stage_bootloader()) {
    // Failed to load BT bootloader. Shutdown if we haven't tried this already.
    PBL_LOG(LOG_LEVEL_ERROR, "Failed to load BLE Second Stage Bootloader - shutting down");
    if (reboot_reason_get_last_reboot_reason() != RebootReasonCode_DialogBootFault) {
      enter_standby(RebootReasonCode_DialogBootFault);
    }
  } else if (dialog_spi_bootloader_load_image() && host_transport_init()) {
    hc_protocol_init();
    bt_local_id_generate_address_from_serial(&config->identity_addr);
    bool result = hc_endpoint_ctl_init_sync(config);
    // Nothing good comes from a failure here. BT gets partially initialized and you won't even see
    // the correct state displayed in the BT Settings Menu. Until PBL-36163 is addressed, the best
    // thing we can really do is reboot the watch.
    PBL_ASSERTN(result);
    return result;
  }

  // We failed to load BT, panic instead of crash looping to provide info
  PebbleEvent event = {
    .type = PEBBLE_PANIC_EVENT,
    .panic = {
      .error_code = ERROR_CANT_LOAD_BT,
    },
  };
  event_put(&event);

  return false;
}

void bt_driver_stop(void) {
  // Do stuff that needs to happen when stack stops.
  if (!hc_endpoint_ctl_shutdown_sync()) {
    // PBL-34091: What if the chip is in a bad state? The message may not have any effect and might
    // keep us in a higher-than-desired power state. How can we be sure the chip is hibernating?
  }

  host_transport_deinit();
  hc_protocol_deinit();
}

void bt_driver_power_down_controller_on_boot(void) {
  // Don't make any assumptions about the state of the BT chip on initial bootup. (since the code
  // runs in RAM) If we want to be in airplane mode, power the chip on then off
  BTDriverConfig config = { };
  if (bt_driver_start(&config)) {
    bt_driver_stop();
  }
}
