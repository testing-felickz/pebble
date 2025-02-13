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

/* Linker script to place sections and symbol values. Should be used together
 * with other linker script that defines memory regions ROM, RetRAM0 and RAM.
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
 *   __end__
 *   end
 *   __StackLimit
 *   __StackTop
 */

#include "da1468x_mem_map.h"

#define VECTOR_TABLE_MAX_SIZE       (0xC0 + 0x80)
#define VECTOR_TABLE_ADDRESS        (DATA_RAM_BASE_ADDRESS)
#define HEAP_START_ADDRESS          (VECTOR_TABLE_ADDRESS + VECTOR_TABLE_MAX_SIZE)
#define CODE_AND_RAM_BASE_ADDRESS   (HEAP_START_ADDRESS + configTOTAL_HEAP_SIZE)

#define INFOBLOB_SIZE (VECTOR_TABLE_MAX_SIZE + 8 /* size of fields in .ObjectBinInfo below */ )

MEMORY
{
  VT (rx):
    ORIGIN = VECTOR_TABLE_ADDRESS,
    LENGTH = VECTOR_TABLE_MAX_SIZE
  INFOBLOB (r):
    ORIGIN = CODE_AND_RAM_BASE_ADDRESS - INFOBLOB_SIZE,
    LENGTH = INFOBLOB_SIZE
  CODE_AND_RAM (rwx):
    ORIGIN = CODE_AND_RAM_BASE_ADDRESS,
    LENGTH = DATA_RAM_BASE_ADDRESS + DATA_RAM_SIZE - CODE_AND_RAM_BASE_ADDRESS
  CACHE_RAM (rwx):
    ORIGIN = CACHE_RAM_BASE_ADDRESS
    LENGTH = CACHE_RAM_SIZE

  /* Allocate log strings here for the console. Not loaded to memory. */
  LOG_STRINGS (r) :
    ORIGIN = 0xC0000000,
    LENGTH = 512K

  /* Allocate Firmware Metadata here. Not loaded to memory. See below. */
  FW_VERSION (r) :
    ORIGIN = 0xD0000000,
    LENGTH = 1K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .vt :
  {
    KEEP(*(.isr_vector))
    /* make sure that IVT doesn't cross 0xC0 */
    . = 0xC0;
    KEEP(*(.patch_table))
    /* make sure that IVT is the correct size */
    . = VECTOR_TABLE_MAX_SIZE;
    } > VT
    /* The heap starts immediately after the patch table and runs to the start of .zero.table */
    __heap_start = .;
    __HEAP_START_ADDR_CHECK = HEAP_START_ADDRESS;
    ASSERT((__heap_start == __HEAP_START_ADDR_CHECK), "Heap start address is incorrect!")

  // Note: There is a struct in dialog_spi_bootloader.c that matches this section
  .ObjectBinInfo  :
  {
    LONG(CODE_AND_RAM_BASE_ADDRESS)
    LONG(VECTOR_TABLE_MAX_SIZE)
    // Note: if you add/remove something, don't forget to change INFOBLOB_SIZE above
  } > INFOBLOB

  // When we build the binary with objcopy, we pack the vector & patch table here
  .vt_stash_region (COPY) :
  {
    . = . + VECTOR_TABLE_MAX_SIZE;
  } > INFOBLOB

   /* GNU build id: This is a hash of parts of the binary that uniquely
    * identifies the binary. This hash gets inserted by the linker;
    * we're passing the flag --build-id=sha1 to do this.
    * The variable DIALOG_BUILD_ID is provided, so that the values can be used
    * in the firmware code. */
  .note.gnu.build-id : {
    PROVIDE(DIALOG_BUILD_ID = .);
    KEEP(*(.note.gnu.build-id))
  } > CODE_AND_RAM

  /* To clear multiple BSS sections,
   * uncomment .zero.table section and,
   * define __STARTUP_CLEAR_BSS_MULTIPLE in startup_ARMCMx.S */
  .zero.table :
  {
    . = ALIGN(4);
    __zero_table_start__ = .;
    LONG (HEAP_START_ADDRESS)
    LONG (configTOTAL_HEAP_SIZE)
    LONG (__zero_initialized_start__)
    LONG (__zero_initialized_end__ - __zero_initialized_start__)
    LONG (__ble_vars_start__)
    LONG (__ble_vars_end__ - __ble_vars_start__)
    LONG (__cache_ram_zi_start__)
    LONG (__cache_ram_zi_end__ - __cache_ram_zi_start__)
    LONG (__log_buffer_start__)
    LONG (__log_buffer_end - __log_buffer_start__)
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
    *(retention_mem_rw)
    *(privileged_data_rw)
    *(.retention)
    . = ALIGN(4);

    /* All data end */
    __data_end__ = .;

  } > CODE_AND_RAM

  .text :
  {
    __text_start = .;
    /*
     * Dialog assigns their jump table to a special section (I'm not
     * sure why). Need to add it since the linker will otherwise try
     * to place it in the first available region (the vector table region).
     */
    KEEP(*(jump_table_mem_area))
    KEEP(*(.default_patch_code_handler_section))

    *(.text*)
    . = ALIGN(4);
    *(text_retained)

    *(.rodata*)

    KEEP(*(.eh_frame*))
    . = ALIGN(4);
  } > CODE_AND_RAM = 0x00
  __text_end = .;

  .text_crc32 :
  {
    LONG(0xDEADBEEF)
  } > CODE_AND_RAM

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } > CODE_AND_RAM = 0x00

  __exidx_start = .;
  .ARM.exidx :
  {
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > CODE_AND_RAM = 0x00
  __exidx_end = .;

  __StackTop = __StackLimit + __STACK_SIZE;
  PROVIDE(__stack = __StackTop);

  __zero_initialized_start__ = .;

  /* .stack_dummy section doesn't contains any symbols. It is only a
   * a placeholder for where the ISR stack lives. It's put right after the
   * .text section, so that when it overflows, it's easy to figure out because
   * .text is not supposed to be changed.
   */
  .stack_dummy (NOLOAD):
  {
    . = ALIGN(4);
    __StackLimit = .;
    . += __STACK_SIZE;
  } > CODE_AND_RAM

  .bss (NOLOAD):
  {
    . = ALIGN(4);
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    *(retention_mem_zi)
    . = ALIGN(4);
  } > CODE_AND_RAM

  . = ALIGN(4);  /* zero initing startup-code assumes word-alignment */
  __zero_initialized_end__ = .;

  __debug_region_start__ = .;
  .debug_region (NOLOAD):
  {
    KEEP(*(reboot_reason))
  } > CODE_AND_RAM
  __debug_region_end__ = .;


  /* Put the debug log buffer in it's own section. This allows us to dynamically grow it
   * to use all available RAM (well, up to RETENTION_BLE).
   */
  .log_buffer (NOLOAD) :
  {
    __log_buffer_start__ = .;
    KEEP(*(.log_buffer))
    __log_buffer_end = .;
  } > CODE_AND_RAM


  /* BLE ROM (RivieraWaves) variables -- see BLE_VAR_ADDR
     This region extends all the way to the end of SysRAM!
   */
  RETENTION_BLE BLE_VAR_ADDR (NOLOAD) :
  {
    __ble_vars_start__ = .;
    KEEP(*(ble_variables))
  } > CODE_AND_RAM

  . = ALIGN(4);  /* zero initing startup-code assumes word-alignment */
  __ble_vars_end__ = .;

  .cache_ram (NOLOAD):
  {
    __cache_ram_zi_start__ = .;
    *(ble_env_heap)
    *(ble_msg_heap)
    *(ble_db_heap)
    . = ALIGN(4);
    *(privileged_data_zi)

    . = ALIGN(4);  /* zero initing startup-code assumes word-alignment */
    __cache_ram_zi_end__ = .;
  } > CACHE_RAM

  /* Unloaded section containing our log strings. */
  .log_strings (INFO) : {
      KEEP(*(.log_string.header))
      KEEP(*(.log_strings))
  } >LOG_STRINGS

  /* Unloaded section containing our Firmware Metadata.
   * If there is ever enough RAM to keep this in the RAM image (at the very end of .text)
   * this section can be safely removed without causing errors in the coredump/feelsbadman tools
   */
  .fw_version (INFO) : {
      KEEP(*(.pbl_fw_version))
  } >FW_VERSION




  /* Symbols for the core_dump memory table */
  __vector_table_length = LENGTH(VT);
  __heap_length = __zero_table_start__ - __heap_start;
  __text_length = __text_end - __text_start;
  __rwdata_length = __data_end__ - __data_start__;
  __stack_bss_length = __zero_initialized_end__ - __zero_initialized_start__;
  __debug_region_length = __debug_region_end__ - __debug_region_start__;
  __ble_vars_length = __ble_vars_end__ - __ble_vars_start__;
  __cache_ram_length = __cache_ram_zi_end__ - __cache_ram_zi_start__;
}
