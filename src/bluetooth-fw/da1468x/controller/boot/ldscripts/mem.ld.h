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

#include "da1468x_mem_map.h"

/* Linker script to place sections and symbol values. Should be used together
 * with other linker script that defines memory regions CODE_AND_RAM
 * It references following symbols, which must be defined in code:
 *   Reset_Handler : Entry of reset handler
 *
 * It defines following symbols, which code can use without definition:
 *   __zero_table_start__
 *   __zero_table_end__
 *   __etext
 *   __data_start__
 *   __data_end__
 *   __bss_start__
 *   __bss_end__
 *   __StackLimit
 *   __StackTop
 *   __stack
 */

MEMORY
{
  CODE_AND_RAM (rwx): ORIGIN = DATA_RAM_BASE_ADDRESS, LENGTH = 10 * 1024
}

ENTRY(Reset_Handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    /* make sure that IVT doesn't cross 0xC0 */
    . = 0xC0;

    KEEP(*(.patch_table))
    KEEP(*(.default_patch_code_handler_section))

    *(.text*)

    *(.rodata*)

    KEEP(*(.eh_frame*))
  } > CODE_AND_RAM

  /* To clear multiple BSS sections,
   * uncomment .zero.table section and,
   * define __STARTUP_CLEAR_BSS_MULTIPLE in startup_ARMCMx.S */
  .zero.table :
  {
    . = ALIGN(4);
    __zero_table_start__ = .;
    LONG (__bss_start__)
    LONG (__bss_end__ - __bss_start__)
    __zero_table_end__ = .;
  } > CODE_AND_RAM

  __etext = .;

  /* The initialised data section is stored immediately
  at the end of the text section */
  .data : AT (__etext)
  {
    __data_start__ = .;
    *(vtable)
    *(.data*)

    . = ALIGN(4);
    /* init_array/fini_array moved to flash, align preserved */

    KEEP(*(.jcr*))
    . = ALIGN(4);
    /* All data end */
    __data_end__ = .;
  } > CODE_AND_RAM

  /* GNU build id: This is a hash of parts of the binary that uniquely
   * identifies the binary. This hash gets inserted by the linker;
   * we're passing the flag --build-id=sha1 to do this.
   * The variable DIALOG_BUILD_ID is provided, so that the values can be used
   * in the firmware code. */
  .note.gnu.build-id : {
     PROVIDE(DIALOG_BUILD_ID = .);
     KEEP(*(.note.gnu.build-id))
  } > CODE_AND_RAM

  .bss :
  {
    . = ALIGN(4);
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    *(retention_mem_zi)
    . = ALIGN(4);
    __bss_end__ = .;
  } > CODE_AND_RAM

  /* .stack_dummy section doesn't contains any symbols. It is only a
   * a placeholder for where the ISR stack lives. */
  .stack_dummy (COPY):
  {
    __StackBottom = .;
    *(.stack*)
  } > CODE_AND_RAM

  /* Set stack top to end of RAM, and stack limit move down by
   * size of stack_dummy section */
  __StackTop = ORIGIN(CODE_AND_RAM) + LENGTH(CODE_AND_RAM);
  __StackLimit = __StackTop - SIZEOF(.stack_dummy);
  PROVIDE(__stack = __StackTop);
}
