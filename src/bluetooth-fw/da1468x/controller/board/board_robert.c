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

static const BoardConfigHostSPI s_host_spi = {
  .mcu_int = {
    .port = HW_GPIO_PORT_2,
    .pin = HW_GPIO_PIN_3,
    .function = HW_GPIO_FUNC_GPIO,
  },

  .spi = {
    .peripheral = HW_SPI1,
    .cs = {
      .port = HW_GPIO_PORT_0,
      .pin = HW_GPIO_PIN_1,
      .function = HW_GPIO_FUNC_SPI_EN,
    },
    .cs_2 = {
      .port = HW_GPIO_PORT_0,
      .pin = HW_GPIO_PIN_7,
      .function = HW_GPIO_FUNC_GPIO,
    },
    .clk = {
      .port = HW_GPIO_PORT_0,
      .pin = HW_GPIO_PIN_0,
      .function = HW_GPIO_FUNC_SPI_CLK,
    },
    .miso_do = {
      .port = HW_GPIO_PORT_1,
      .pin = HW_GPIO_PIN_3,
      .function = HW_GPIO_FUNC_SPI_DO,
    },
    .mosi_di = {
      .port = HW_GPIO_PORT_0,
      .pin = HW_GPIO_PIN_4,
      .function = HW_GPIO_FUNC_SPI_DI,
    },
  }
};

// These are spare GPIOs on both the SILK and ROBERT bigboards which are broken out to a header
// Check out debug_gpio.h for more info on how to leverage them for debug
static BoardConfigGpioDebug s_debug_gpios = {
  .num_debug_gpios = 4,
  .debug_gpio = {
    [0] = {
      .port = HW_GPIO_PORT_3,
      .pin = HW_GPIO_PIN_0,
    },
    [1] = {
      .port = HW_GPIO_PORT_3,
      .pin = HW_GPIO_PIN_1,
    },
    [2] = {
      .port = HW_GPIO_PORT_3,
      .pin = HW_GPIO_PIN_2,
    },
    [3] = {
      .port = HW_GPIO_PORT_3,
      .pin = HW_GPIO_PIN_3,
    },
  }
};


static const BoardConfigBTFEM s_bt_fem = {
  .rx = {
    .port = HW_GPIO_PORT_1,
    .pin = HW_GPIO_PIN_6,
  },
  .tx = {
    .port = HW_GPIO_PORT_1,
    .pin = HW_GPIO_PIN_7,
  },
};

const BoardConfigHostSPI * const HOST_SPI = &s_host_spi;
BoardConfigGpioDebug * const DEBUG_GPIOS = &s_debug_gpios;
const BoardConfigBTFEM * const BT_FEM = &s_bt_fem;
