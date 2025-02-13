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

#include "hw_gpio.h"
#include "hw_spi.h"

#include <stdbool.h>

typedef struct GPIOPortPin {
  HW_GPIO_PORT port;
  HW_GPIO_PIN pin;
  HW_GPIO_FUNC function;
} GPIOPortPin;

typedef struct {
  // BT Controller to MCU INT line:
  GPIOPortPin mcu_int;

  // BT Controller SPI slave:
  struct {
    HW_SPI_ID *peripheral;
    GPIOPortPin cs;
    GPIOPortPin cs_2;
    GPIOPortPin clk;
    GPIOPortPin miso_do;
    GPIOPortPin mosi_di;
  } spi;
} BoardConfigHostSPI;

typedef struct DebugGPIOInfo {
  HW_GPIO_PORT port;
  HW_GPIO_PIN pin;
  bool is_active;
} DebugGPIOInfo;

typedef struct BoardConfigGpioDebug {
  int num_debug_gpios;
  DebugGPIOInfo debug_gpio[];
} BoardConfigGpioDebug;

typedef struct {
  // BT FEM
  GPIOPortPin rx;
  GPIOPortPin tx;
} BoardConfigBTFEM;

extern const BoardConfigHostSPI * const HOST_SPI;
extern BoardConfigGpioDebug * const DEBUG_GPIOS;
extern const BoardConfigBTFEM * const BT_FEM;

#include "board_definitions.h"
