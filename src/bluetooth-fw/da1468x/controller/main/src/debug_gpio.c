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

#include "board.h"
#include "system/logging.h"

// Dialog SDK
#include <hw_gpio.h>

static void prv_config_gpio_as_output_and_set_state(DebugGPIOInfo *gpio, bool active) {
  // While GPIO pin itself can retain state when the system enters deep sleep the PXX_MODE_REG
  // holding the configuration gets reset so reprogram the gpio pin function
  hw_gpio_set_pin_function(gpio->port, gpio->pin, HW_GPIO_MODE_OUTPUT, HW_GPIO_FUNC_GPIO);
  if (active) {
    hw_gpio_set_active(gpio->port, gpio->pin);
  } else {
    hw_gpio_set_inactive(gpio->port, gpio->pin);
  }

  gpio->is_active = active;
}

void debug_gpio_init(void) {
  for (int i = 0; i < DEBUG_GPIOS->num_debug_gpios; i++) {
    prv_config_gpio_as_output_and_set_state(&DEBUG_GPIOS->debug_gpio[i], false);
  }
}

void debug_gpio_toggle(int debug_gpio_num) {
  if (debug_gpio_num >= DEBUG_GPIOS->num_debug_gpios) {
    PBL_LOG(LOG_LEVEL_WARNING, "Invalid debug gpio id");
    return;
  }

  DebugGPIOInfo *gpio = &DEBUG_GPIOS->debug_gpio[debug_gpio_num];
  prv_config_gpio_as_output_and_set_state(gpio, !gpio->is_active);
}

void debug_gpio_set_active(int debug_gpio_num, bool is_active) {
  if (debug_gpio_num >= DEBUG_GPIOS->num_debug_gpios) {
    PBL_LOG(LOG_LEVEL_WARNING, "Invalid debug gpio id");
    return;
  }

  DebugGPIOInfo *gpio = &DEBUG_GPIOS->debug_gpio[debug_gpio_num];
  prv_config_gpio_as_output_and_set_state(gpio, is_active);
}
