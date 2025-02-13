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

#include "core_dump.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "sdk_defs.h"
#include "hw_spi.h"
#include "hw_timer0.h"
#include "hw_timer1.h"
#include "hw_watchdog.h"
#include "sys_power_mgr.h"
#include "sys_clock_mgr.h"

#include "da1468x_mem_map.h"
#include "platform_devices.h"
#include "util/build_id.h"
#include "util/crc32.h"

#include "host_transport_impl.h"
#include "host_transport_protocol.h"
#include "reboot_reason.h"

#define TIMER0_CLK_RATE (32000)
#define TIMER0_RELOAD_VALUE (65535)
#define TIMER0_1_S (TIMER0_CLK_RATE / 1)
#define TIMER0_1_MS (TIMER0_CLK_RATE / 1000)
#define TIMER0_10_MS (TIMER0_CLK_RATE / 100)

// Modified from ad_spi.c. We don't want to use DMA here!
#define CORE_DUMP_SPI_SLAVE_TO_EXT_MASTER(bus, name, _word_mode, pol_mode, _phase_mode, \
    _dma_channel) \
  const spi_device_config dev_##name = { \
    .bus_id = HW_##bus, \
    .bus_res_id = RES_ID_##bus, \
    .hw_init.smn_role = HW_SPI_MODE_SLAVE, \
    .hw_init.cs_pad = { 0, 0 }, \
    .hw_init.word_mode = _word_mode, \
    .hw_init.polarity_mode = pol_mode, \
    .hw_init.phase_mode = _phase_mode, \
    .hw_init.xtal_freq = 0, \
    .hw_init.fifo_mode = HW_SPI_FIFO_RX_TX, \
    .hw_init.disabled = 0, \
    .hw_init.use_dma = _dma_channel >= 0, \
    .hw_init.rx_dma_channel = _dma_channel, \
    .hw_init.tx_dma_channel = _dma_channel + 1, \
    };

CORE_DUMP_SPI_SLAVE_TO_EXT_MASTER(SPI1, CORE_DUMP_SPI, CONFIG_SPI_WORD_MODE,
                                  CONFIG_SPI_POL_MODE, CONFIG_SPI_PHASE_MODE,
                                  CONFIG_SPI_DMA_CHANNEL);

void DMA_Handler(void);  // This is the Dialog SPI/DMA Interrupt handler


static volatile bool s_spi_dma_complete;

static const spi_device_config * const s_spi = &dev_CORE_DUMP_SPI;

static bool s_core_dump_initiated = false;
extern uint32_t *g_stacked_regs;

// The following structures are accessed below in __asm. Because the Cortex-M0 doesn't handle
// unaligned accesses automatically, and we don't want to do it ourselves, let's force the
// elements we care about to be aligned correctly.
typedef struct {
  int8_t padding[3];
  CoreDumpThreadInfo ti;
} CoreDumpThreadInfo_Wrapper;
static CoreDumpThreadInfo_Wrapper ALIGN(4) s_thread_info;
_Static_assert(((uint32_t)&s_thread_info.ti.registers[0] % 4) == 0,
               "Structure not correctly aligned");
static CoreDumpExtraRegInfo ALIGN(4) s_extra_info;


extern void debug_uart_init(void);

// Linker symbols
extern const uint8_t __heap_start;
extern const uint8_t __zero_table_start;
extern const uint8_t __text_start;
extern const uint8_t __text_end;
extern const uint8_t __data_start__;
extern const uint8_t __zero_initialized_start__;
extern const uint8_t __ble_vars_start__;
extern const uint8_t __cache_ram_zi_start__;
extern const uint8_t __debug_region_start__;

extern const uint32_t __vector_table_length[];
extern const uint32_t __heap_length[];
extern const uint32_t __text_length[];
extern const uint32_t __rwdata_length[];
extern const uint32_t __stack_bss_length[];
extern const uint32_t __ble_vars_length[];
extern const uint32_t __cache_ram_length[];
extern const uint32_t __debug_region_length[];

extern const ElfExternalNote DIALOG_BUILD_ID;

static const MemoryRegion s_MemoryRegions[MemoryRegionTagCount] = {
  { .tag = MemoryRegionTag_Text,  // .text is first so it's easily excluded on crc check
    .start = &__text_start,
    .length = (uint32_t)__text_length,
  },
  { .tag = MemoryRegionTag_VectorTable,
    .start = (void *)DATA_RAM_BASE_ADDRESS,
    .length = (uint32_t)__vector_table_length,
  },
  { .tag = MemoryRegionTag_Heap,
    .start = &__heap_start,
    .length = (uint32_t)__heap_length,
  },
  { .tag = MemoryRegionTag_RwData,
    .start = &__data_start__,
    .length = (uint32_t)__rwdata_length,
  },
  { .tag = MemoryRegionTag_StackAndBss,
    .start = &__zero_initialized_start__,
    .length = (uint32_t)__stack_bss_length,
  },
  { .tag = MemoryRegionTag_BleVariables,
    .start = &__ble_vars_start__,
    .length = (uint32_t)__ble_vars_length,
  },
  { .tag = MemoryRegionTag_CacheRam,
    .start = &__cache_ram_zi_start__,
    .length = (uint32_t)__cache_ram_length,
  },
  {
    .tag = MemoryRegionTag_RebootReason,
    .start = &__debug_region_start__,
    .length = (uint32_t)__debug_region_length,
  },
};

static uint16_t prv_timer_get_ticks(void) {
  return hw_timer0_get_on();
}

static uint16_t prv_timer_delta(uint16_t start_ticks, uint16_t end_ticks) {
  // The timer is configured to repeatedly count down from 65535, resetting at 0 back to 65535.
  // The delta calculated here takes advantage of the underflow to produce the correct result.
  return start_ticks - end_ticks;
}

static bool prv_timer_check_delta(uint16_t start_ticks, uint16_t delta_ticks) {
  uint16_t curr_ticks  = prv_timer_get_ticks();
  bool s = (prv_timer_delta(start_ticks, curr_ticks) < delta_ticks);
  return s;
}

static void prv_timer_delay_1ms(void) {
  uint16_t start_ticks = prv_timer_get_ticks();
  while (prv_timer_check_delta(start_ticks, TIMER0_1_MS)) {};
}


static NORETURN prv_low_power_mode(void) {
  // Give the host a few seconds to reboot the chip before we attempt to power everthing off (If
  // everything gets powered off the BT reboot reason persisted in RAM (and flushed to analytics on
  // reboot) will be lost)
  for (int i = 0; i < 4; i++) {
    hw_watchdog_set_pos_val(0xFF);
    uint16_t wd_start_ticks = prv_timer_get_ticks();
    while (prv_timer_check_delta(wd_start_ticks, TIMER0_1_S)) {
    }
  }

  REG_SET_BIT(CRG_TOP, CLK_RADIO_REG, BLE_LP_RESET); /* reset BLE LP timer */

  hw_timer0_disable();
  hw_timer1_disable();

  // The following block of code was lifted from sys_power_mgr.c:apply_wfi().
  //
  // TODO: It's not entirely clear what's going on, but it does reduce current draw to ~0 uA. At
  //       some point, it's probably worth figuring out how to cleanly hibernate (PBL-42430)
  hw_cpm_enable_clockless();
  hw_cpm_disable_xtal32k();
  SCB->SCR |= (1 << 2);
  cm_sys_clk_sleep(true);
  hw_cpm_pll_sys_off();
  hw_cpm_activate_pad_latches();
  hw_cpm_power_down_periph_pd();
  hw_cpm_wait_per_power_down();
  hw_cpm_wait_rad_power_down();
  hw_cpm_rfcu_clk_off();
  hw_cpm_disable_rc16();
  hw_cpm_dcdc_off();
  hw_cpm_ldo_io_ret_off();

  hw_watchdog_freeze();

  while (true) {
    __WFI();
  }

  __builtin_unreachable();
}

// Timer0 is very limited -- there is no way to reset the count until it's triggered, so it's
// not useful as a generic one-shot. We'll let it run free and calculate deltas.
static void prv_timer_enable(void) {
  timer0_config cfg = {
    .clk_src = HW_TIMER0_CLK_SRC_SLOW,
    .on_clock_div = false,
    .on_reload = TIMER0_RELOAD_VALUE,
  };
  hw_timer0_init(&cfg);
  hw_timer0_enable();
}

// The initial handshake is required because a) we can't use INT as a busy/ready signal, and b)
// we can't be sure that the host isn't still clocking out data from the previous command.
// Shift incoming data through a pattern buffer. When we get a match, send our response.
static bool prv_initial_handshake(void) {
  uint8_t rx_index = 0;
  uint8_t tx_index = 0;
  uint8_t buffer[sizeof(core_dump_connect_ping)];
  uint16_t tx_start_ticks;
  bool rx_mode;

  printf("Waiting for host:\n");

  for (int count = 10; count > 0; --count) {
    printf(".");
    // Feed the watchdog
    hw_watchdog_set_pos_val(0xFF);

    // Configure for RX mode
    rx_mode = true;
    hw_spi_change_fifo_mode(s_spi->bus_id, HW_SPI_FIFO_RX_ONLY);

    // Spin for 1 second before handling the watchdog/INT line
    uint16_t wd_start_ticks = prv_timer_get_ticks();
    while (prv_timer_check_delta(wd_start_ticks, TIMER0_1_S)) {
      if (rx_mode) {
        // Has a byte arrived?
        if (!hw_spi_get_interrupt_status(s_spi->bus_id)) {
          continue;
        }
        hw_spi_clear_interrupt(s_spi->bus_id);
        buffer[rx_index++] = hw_spi_fifo_read8(s_spi->bus_id);

        if (rx_index == sizeof(core_dump_connect_ping)) {
          if (memcmp(buffer, core_dump_connect_ping, sizeof(core_dump_connect_ping)) == 0) {
            // Handshake received. Switch to tx mode.
            hw_spi_change_fifo_mode(s_spi->bus_id, HW_SPI_FIFO_TX_ONLY);
            rx_mode = false;
            tx_index = 0;
            tx_start_ticks = prv_timer_get_ticks();
            continue;
          } else {
            // Buffer is full. Shift everything down one byte.
            rx_index--;
            memmove(&buffer[0], &buffer[1], rx_index);
          }
        }
      } else { // Tx Mode
        if (hw_spi_is_tx_fifo_full(s_spi->bus_id)) {
          // Don't spin for too long, just in case the host doesn't clock out our response
          if (!prv_timer_check_delta(tx_start_ticks, TIMER0_10_MS)) {
            break;
          }
          continue;
        }
        hw_spi_fifo_write8(s_spi->bus_id, core_dump_connect_response[tx_index++]);
        if (tx_index == sizeof(core_dump_connect_response)) {
          return true;
        }
      }
    }

    // Pulse the INT line to wake the host
    host_transport_set_mcu_int(true);
    prv_timer_delay_1ms(); // The timer IRQ just fired so the counter won't be near roll-over
    host_transport_set_mcu_int(false);
  }

  return false;
}

// Callback for SPI DMA complete
static void prv_spi_dma_tx_cb(void *user_data, uint16_t transferred) {
  s_spi_dma_complete = true;
}

// Wait for DMA to complete. Note that since we don't have interrupts enabled, we're polling for
// the interrupt and then calling the DMA Interrupt Handler directly. This will cause the
// callback above to be called.
static void prv_wait_for_dma(bool is_tx_only) {
  while (!s_spi_dma_complete) {
    if (NVIC_GetPendingIRQ(DMA_IRQn)) {
      NVIC_ClearPendingIRQ(DMA_IRQn);
      DMA_Handler();
    }
  }

  // NOTE: It looks like SPI/DMA complete interrupts will fire when the data fills the FIFO, not
  // when the FIFO is actually empty. Let's spin until the FIFO is drained, if we're transmitting.
  // If we're receiving, this function will spin forever. See ad_spi.c:322.
  if (is_tx_only) {
    hw_spi_wait_while_busy(s_spi->bus_id);
  }
}

static void prv_get_cmd(uint8_t *buffer, uint8_t len) {


  s_spi_dma_complete = false;
  hw_spi_read_buf(s_spi->bus_id, buffer, len, prv_spi_dma_tx_cb, NULL);

  // Now signal the host that we're ready to RX.
  host_transport_set_mcu_int(true);
  prv_wait_for_dma(false);
  host_transport_set_mcu_int(false);
}

static void prv_send_response(const uint8_t *buffer, uint16_t len) {
  uint32_t crc = crc32(CRC32_INIT, buffer, len);

  s_spi_dma_complete = false;
  hw_spi_write_buf(s_spi->bus_id, buffer, len, prv_spi_dma_tx_cb, NULL);

  // Now signal the host that we're ready to TX.
  host_transport_set_mcu_int(true);
  prv_wait_for_dma(true);
  host_transport_set_mcu_int(false);

  prv_timer_delay_1ms();

  // Now send the CRC
  s_spi_dma_complete = false;
  hw_spi_write_buf(s_spi->bus_id, (void *)&crc, sizeof(crc), prv_spi_dma_tx_cb, NULL);

  host_transport_set_mcu_int(true);
  prv_wait_for_dma(true);
  host_transport_set_mcu_int(false);
}

static void prv_cmd_handler(bool text_crc_matches) {
  CoreDumpSPICmd cmd;

  printf("\nHost connected\n");

  while (true) {
    // Feed the watchdog
    hw_watchdog_set_pos_val(0xFF);

    prv_get_cmd((uint8_t *)&cmd, sizeof(cmd));
    if (crc32(CRC32_INIT, &cmd, sizeof(cmd)) != CRC32_RESIDUE) {
      printf("cmd CRC failed");
      prv_low_power_mode();
    }

    // Feed the watchdog
    hw_watchdog_set_pos_val(0xFF);

    switch (cmd.cmd) {
      case CoreDumpCmd_GetTextCRC: {
        uint16_t response = text_crc_matches; // This can't be a bool (1-byte). The Dialog SPI/DMA
                                              // Tx will lock-up and not signal completion.
        printf("get text crc %d\n", sizeof(response));
        prv_send_response((uint8_t *)&response, sizeof(response));
        }
        break;

      case CoreDumpCmd_GetBuildID:
        printf("build id\n");
        prv_send_response((uint8_t *)&DIALOG_BUILD_ID, BUILD_ID_TOTAL_EXPECTED_LEN);
        break;

      case CoreDumpCmd_ReadRegionTable:
        printf("region table\n");
        prv_send_response((uint8_t *)s_MemoryRegions, sizeof(s_MemoryRegions));
        break;

      case CoreDumpCmd_ReadMemory: {
        printf("read memory %p %d\n", cmd.addr, cmd.len);
        prv_send_response(cmd.addr, cmd.len);
        }
        break;

      case CoreDumpCmd_ReadRunningThreadInfo:
        printf("read TI\n");
        prv_send_response((uint8_t *)&s_thread_info.ti, sizeof(s_thread_info.ti));
        break;

      case CoreDumpCmd_ReadExtraThreadInfo:
        printf("read ExtraTB\n");
        prv_send_response((uint8_t *)&s_extra_info, sizeof(s_extra_info));
        break;

      case CoreDumpCmd_LowPowerMode:
        printf("low power mode\n");
        prv_low_power_mode();
        break;

      default:
        break;
    }
  }
}

NORETURN core_dump(bool user_requested) {
  // Feed the watchdog before we continue
  hw_watchdog_set_pos_val(0xFF);

  // Disable interrupts, should we not have been called from fault context
  portDISABLE_INTERRUPTS();

  // Reconfig debug serial
  debug_uart_init();

  printf("\n\nStarting Core Dump\n");

  // Big problem if we re-enter here - it likely means we encountered and exception during
  // the core dump
  if (s_core_dump_initiated) {
    printf("CD: re-entered\n");

    // Update the reboot reason. Preserve the LR.
    RebootReason reason;
    reboot_reason_get(&reason);
    reason.code = RebootReasonCode_CoreDumpReentered;
    reboot_reason_set(&reason);

    // Go into low-power mode. Hard reset isn't useful.
    prv_low_power_mode();
  }
  s_core_dump_initiated = true;


  if (user_requested) {
    printf("CD: user requested\n");
    RebootReason reason = { .code = RebootReasonCode_CoreDumpRequested };
    reboot_reason_set(&reason);
  }

  // Save the registers that would have been stacked by a fault into CoreDumpThreadInfo struct
  uint32_t *regs = (uint32_t *)s_thread_info.ti.registers;
  if (user_requested) {
    __asm volatile (
      "  mov r0, %[regs] \n"
      "  add r0, r0, #4               \n"       // skip over r0, we can't save it
      "  stmia r0!, {r1-r3}           \n"       // save r1-r3
      "  add r0, r0, #32              \n"       // skip r4-r11 (8 * 4bytes)

      "  mov r1, r12                  \n"       // save r12
      "  str r1, [r0]                 \n"
      "  add r0, r0, #4               \n"

      "  mov r1, lr                   \n"       // save lr
      "  str r1, [r0]                 \n"
      "  add r0, r0, #4               \n"

      "  mov r1, pc                   \n"       // save pc
      "  str r1, [r0]                 \n"
      "  add r0, r0, #4               \n"

      "  mrs r1, xpsr                 \n"       // save xpsr
      "  str r1, [r0]                 \n"
      "  add r0, r0, #4               \n"

      : [regs] "+r" (regs)
      :
      : "r0", "r1"
      );
  } else if (g_stacked_regs != NULL) {
    // Copy the stacked registers from the stacked register set
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_R0] = g_stacked_regs[Stacked_Register_R0];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_R1] = g_stacked_regs[Stacked_Register_R1];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_R2] = g_stacked_regs[Stacked_Register_R2];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_R3] = g_stacked_regs[Stacked_Register_R3];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_R12] = g_stacked_regs[Stacked_Register_R12];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_LR] = g_stacked_regs[Stacked_Register_LR];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_PC] = g_stacked_regs[Stacked_Register_PC];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_XPSR] = \
                                                           g_stacked_regs[Stacked_Register_xPSR];
    s_thread_info.ti.registers[portCANONICAL_REG_INDEX_SP] = (uint32_t)g_stacked_regs;
  }

  // Copy the remaining registers
  regs = (uint32_t *)&s_thread_info.ti.registers;
  __asm volatile (
    "  mov r0, %[regs]              \n"
    "  add r0, r0, #16              \n"       // skip r0-r3
    "  stmia r0!, {r4-r7}           \n"       // save r4-r7

    "  mov r4, r8                   \n"       // save r8-r11
    "  mov r5, r9                   \n"
    "  mov r6, r10                  \n"
    "  mov r7, r11                  \n"
    "  stmia r0!, {r4-r7}           \n"

    : [regs] "+r" (regs)
    :
    : "r0", "r1", "r4", "r5", "r6", "r7"
    );

  // Copy the extra registers
  regs = (uint32_t *)&s_extra_info;
  __asm volatile (
    "  mov r0, %[regs] \n"

    "  mrs r1, msp                  \n"       // msp
    "  mrs r2, psp                  \n"       // psp
    "  stmia r0!, {r1-r2}           \n"

    "  mrs r3, primask              \n"       // primask
    "  strb r3, [r0]                \n"
    "  add r0, r0, #3               \n"       // skip basepri & faultmask -- not on CM0

    "  mrs r1, control              \n"       // control
    "  strb r1, [r0]                \n"

    : [regs] "+r" (regs)
    :
    : "r0", "r1"
    );

  // Fill in the rest of the CoreDumpThreadInfo struct
  bool in_isr_task = ((s_thread_info.ti.registers[portCANONICAL_REG_INDEX_XPSR] & 0x1FF) != 0);

  if (in_isr_task) {
    strncpy((char *)s_thread_info.ti.name, "ISR", sizeof(s_thread_info.ti.name));
    s_thread_info.ti.id = (uint32_t)1;
    s_thread_info.ti.running = false;
  } else {
    TaskHandle_t current_task_id = xTaskGetCurrentTaskHandle();
    char *task_name = pcTaskGetTaskName(current_task_id);
    s_thread_info.ti.running = true;
    s_thread_info.ti.id = (uint32_t)current_task_id;
    if (task_name) {
      strncpy((char *)s_thread_info.ti.name, task_name, sizeof(s_thread_info.ti.name));
    }
  }

  // Make sure that our stack pointer is somewhere rational
  register uint32_t *sp __asm("sp");
  extern uint32_t __StackLimit, __StackTop;
  uint32_t *stack_limit = &__StackLimit;
  uint32_t *stack_top = &__StackTop;
  printf("Stack: %p - %p\n", stack_limit, stack_top);
  printf("Our SP = %p\n", sp);
  if (!((stack_limit <= sp) || (sp <= stack_top))) {
    printf("Stack is not sane (%p)! Best of luck.\n", sp);
  }

  // Calculate the Application CRC
  uint32_t *text_start = (uint32_t *)&__text_start;
  uint32_t *text_end = (uint32_t *)&__text_end;
  size_t text_length = (uint32_t)__text_length;
  uint32_t text_crc_calculated, text_crc_from_image;
  printf(".text: %p - %p, 0x%zX\n", text_start, text_end, text_length);
  text_crc_calculated = crc32(CRC32_INIT, text_start, text_length);
  text_crc_from_image = *text_end; // The CRC is at __exidx_end.
  printf(".text calculated CRC32 = 0x%08" PRIX32 "\n", text_crc_calculated);
  printf(".text from image CRC32 = 0x%08" PRIX32 "\n", text_crc_from_image);
  if (text_crc_calculated != text_crc_from_image) {
    printf(".text CRC32 does not match!\n");
  }

  // Reset the DMA controller
  for (HW_DMA_CHANNEL channel = HW_DMA_CHANNEL_0; channel < HW_DMA_CHANNEL_INVALID; channel++) {
    hw_dma_channel_enable(channel, HW_DMA_STATE_DISABLED);
  }

  // Configure the SPI module.
  hw_spi_reset(s_spi->bus_id);
  hw_spi_init(s_spi->bus_id, &s_spi->hw_init);

  // Configure the hardware/GPIO interface. Use the host_transport code for now.
  host_transport_init_periph();
  host_transport_configure_spi_scs_pin(SCSPinFunction_SPI_CS);
  host_transport_set_mcu_int(false);

  prv_timer_enable();

  // Loop until we get a connection from the host or 10 seconds elapses.
  if (prv_initial_handshake()) {
    prv_cmd_handler(text_crc_calculated == text_crc_from_image);
  }

  printf("Done. Going to sleep");

  // Go to low power mode? Reset doesn't actually get back to the ROM bootrom.
  prv_low_power_mode();
}
