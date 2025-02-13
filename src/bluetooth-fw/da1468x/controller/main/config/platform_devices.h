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

#include <ad_gpadc.h>
#include <ad_i2c.h>
#include <ad_spi.h>
#include <ad_uart.h>

#include "board.h"

SPI_BUS(SPI1)
SPI_SLAVE_TO_EXT_MASTER(SPI1, PEBBLE_HOST, CONFIG_SPI_IGNORE_CS,
                        CONFIG_SPI_WORD_MODE, CONFIG_SPI_POL_MODE,
                        CONFIG_SPI_PHASE_MODE, CONFIG_SPI_DMA_CHANNEL);
SPI_BUS_END

#if dg_configGPADC_ADAPTER

/*
 * Define sources connected to GPADC
 */

GPADC_SOURCE(TEMP_SENSOR, HW_GPADC_CLOCK_INTERNAL, HW_GPADC_INPUT_MODE_SINGLE_ENDED,
             HW_GPADC_INPUT_SE_TEMPSENS, 5, false, HW_GPADC_OVERSAMPLING_1_SAMPLE,
             HW_GPADC_INPUT_VOLTAGE_UP_TO_1V2)

#endif /* dg_configGPADC_ADAPTER */
