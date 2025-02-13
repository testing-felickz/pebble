#!/bin/bash
# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [ "$1" == "main" ]; then
    ELF_PATH=../../../build/src/bluetooth-fw/DA1468x/main/bt_da14681_main.elf
else
    ELF_PATH=../../../build/src/bluetooth-fw/DA1468x/boot/bt_da14681_boot.elf
fi

# We need to update the SYS_CTRL_REG in order to run from RAM and be able to debug:
#   SYS_CTRL_REG (0x50000012): Size 16 bits, Reset Value: 0xb0010.0000, Program Value: 0b1010.1011
#     REMAP_ADR0[2:0]      - 0x3 - Execute from RAMS
#     REMAP_RAMS[4:3]      - 0x1 - DataRAM2, DataRAM1, DataRAM3 (Sequence of 3 first DataRAMs in memory space)
#     PAD_LATCH_EN[5:5]    - 0x1
#     OTPC_RESET_REQ[6:6]  - 0x0
#     DEBUGGER_ENABLE[7:7] - 0x1
#     Remaining 8 bits we just use default for now
#
# Note: The dialog example scripts also issue a "SWD_RESET_REG" by writing "mww 0x400C3050 1"
#       but this seems to jam up openocd so don't do it for now

openocd -f openocd.cfg -c "init; reset halt; mwh 0x50000012 0xAB; load_image $ELF_PATH; reset; shutdown;"
