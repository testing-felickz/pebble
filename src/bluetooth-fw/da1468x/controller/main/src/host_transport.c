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

#include "host_transport.h"
#include "host_transport_protocol.h"
#include "host_transport_impl.h"

#include "hc_protocol/hc_protocol.h"

#include "board.h"
#include "kernel/pbl_malloc.h"
#include "system/logging.h"
#include "system/passert.h"
#include "system/hexdump.h"
#include "tasks.h"
#include "util/attributes.h"
#include "util/circular_buffer.h"
#include "util/crc32.h"

#include "mcu/interrupts.h"

#include <hw_wkup.h>
#include <osal.h>
#include <platform_devices.h>
#include <sdk_defs.h>
#include <stdio.h>
#include <string.h>
#include <sys_watchdog.h>

// Design doc:
// https://docs.google.com/document/d/1or2Ygs3sWt_5XNW_Mpe3Vxmhwuh3DTzdgZr6QlEe7iQ/edit#

#define HOST_TRANSPORT_DEBUG (0)

#if HOST_TRANSPORT_DEBUG
#define HOST_TRANSPORT_DEBUG_LOG(fmt, ...)  PBL_LOG(LOG_LEVEL_DEBUG, fmt, ## __VA_ARGS__)
#else
#define HOST_TRANSPORT_DEBUG_LOG(fmt, ...)
#endif

// Used to Host Logging
bool host_transport_ready = false;

static bool s_is_transacting __RETAINED;
static CircularBuffer s_tx_buffer __RETAINED;
static CircularBuffer s_rx_buffer __RETAINED;
static uint8_t s_tx_storage[HOST_TRANSPORT_CTLR_TX_BUFFER_SIZE] __RETAINED;
static uint8_t s_rx_storage[HOST_TRANSPORT_CTLR_RX_BUFFER_SIZE] __RETAINED;

static void prv_lock(void) {
  OS_ENTER_CRITICAL_SECTION();
}

static void prv_unlock(void) {
  OS_LEAVE_CRITICAL_SECTION();
}

static void prv_core_dump(void) {
  // TODO Implement core dump
  PBL_ASSERTN(0);
}

void host_transport_set_mcu_int(bool is_ready_to_transact) {
  if (is_ready_to_transact) {
    hw_gpio_set_active(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin);
  } else {
    hw_gpio_set_inactive(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin);
  }
}

static void prv_disable_spi_cs_wakeup_interrupt_handling_and_unblock_transaction_loop(void) {
  if (__atomic_test_and_set(&s_is_transacting, __ATOMIC_RELAXED)) {
    // Already transacting
    return;
  }

  // Disable SPI CS wakeup interrupt, otherwise the ISR will keep firing during the SPI transfers.
  HW_WKUP_REG_SETF(CTRL, WKUP_ENABLE_IRQ, 0);

  // Put a task notification to our task to act upon the interrupt:
  if (mcu_state_is_isr()) {
    OS_TASK_NOTIFY_FROM_ISR(DialogTaskList[DialogTask_HostTrans], 0, eNoAction);
  } else {
    OS_TASK_NOTIFY(DialogTaskList[DialogTask_HostTrans], 0, eNoAction);
  }
}

static void prv_reenable_spi_cs_wakeup_interrupt_handling(void) {
  hw_wkup_reset_interrupt();
  HW_WKUP_REG_SETF(CTRL, WKUP_ENABLE_IRQ, 1);
}

static void prv_sample_spi_cs_and_unblock_loop_if_needed(void) {
  // Sample CS, to handle case where the edge was missed:
  bool is_cs_asserted = !hw_gpio_get_pin_status(HOST_SPI->spi.cs.port,
                                                HOST_SPI->spi.cs.pin);
  if (is_cs_asserted && !s_is_transacting) {
    // Edge was missed, pretend interrupt to be fired:
    prv_disable_spi_cs_wakeup_interrupt_handling_and_unblock_transaction_loop();
  }
}

static bool prv_is_scs_asserted(void) {
  return (false == hw_gpio_get_pin_status(HOST_SPI->spi.cs.port, HOST_SPI->spi.cs.pin));
}

// TODO: Can we avoid re-configuring every time? We might be missing a trigger if it happens
// before this reconfiguring is completed... :(
// Dialog Aart: "Unfortunately it is not possible to program an I/O pin with both functions as
// Wake-up and SPI (or other). Either toggle between the 2 functions or define a dedicated GPIO pin
// for Wake-up."
//
// NB: used by core_dump.c
void host_transport_configure_spi_scs_pin(SCSPinFunction function) {
  if (function == SCSPinFunction_Wakeup_GPIO) {
    // Configure SCS to generate a wake up interrupt when the line is pulled down:
    hw_gpio_set_pin_function(HOST_SPI->spi.cs.port, HOST_SPI->spi.cs.pin,
                             HW_GPIO_MODE_INPUT, HW_GPIO_FUNC_GPIO);
  } else {
    // Configure the SCS pin as "SPI Enable" alternate function:
    hw_gpio_set_pin_function(HOST_SPI->spi.cs.port, HOST_SPI->spi.cs.pin,
                             HW_GPIO_MODE_INPUT, HOST_SPI->spi.cs.function);
  }
}

static void prv_spi_chip_select_interrupt_handler(void) {
  // Interrupt handler should always reset interrupt state, otherwise it will be called again.
  hw_wkup_reset_interrupt();

  prv_disable_spi_cs_wakeup_interrupt_handling_and_unblock_transaction_loop();
}

// NB: used by core_dump.c
void init_spi_pins(void) {
  hw_gpio_set_pin_function(HOST_SPI->spi.clk.port, HOST_SPI->spi.clk.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.clk.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.mosi_di.port, HOST_SPI->spi.mosi_di.pin,
                           HW_GPIO_MODE_INPUT, HOST_SPI->spi.mosi_di.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.miso_do.port, HOST_SPI->spi.miso_do.pin,
                           HW_GPIO_MODE_OUTPUT, HOST_SPI->spi.miso_do.function);
  hw_gpio_set_pin_function(HOST_SPI->spi.cs_2.port, HOST_SPI->spi.cs_2.pin,
                           HW_GPIO_MODE_INPUT, HW_GPIO_FUNC_GPIO);

  hw_gpio_configure_pin(HOST_SPI->mcu_int.port, HOST_SPI->mcu_int.pin,
                        HW_GPIO_MODE_OUTPUT, HOST_SPI->mcu_int.function, false /* is_high */);
}

static void prv_do_blocking_spi_transfer(spi_device dev, spi_transfer_data *transfer) {
  ad_spi_complex_transact(dev, transfer, 1, host_transport_set_mcu_int);
}

static bool prv_transact(spi_device dev) {
  const uint8_t *tx_bytes = NULL;

  prv_lock();
  uint16_t tx_bytes_available = circular_buffer_get_read_space_remaining(&s_tx_buffer);
  if (tx_bytes_available) {
    circular_buffer_read(&s_tx_buffer, tx_bytes_available, &tx_bytes, &tx_bytes_available);
  }
  uint8_t *rx_buffer_ptr = NULL;
  uint16_t rx_bytes_available = circular_buffer_write_prepare(&s_rx_buffer, &rx_buffer_ptr);
  prv_unlock();

  // OK to use the stack for these variables,
  // because ad_spi_complex_transact blocks until transfer is completed:
  SPITransportMsgStatus remote_status_in = {};
  SPITransportMsgStatus local_status_out = {
    .msg_id = SPITransportMsgID_Status,
    .bytes_sendable_count = tx_bytes_available,
    .bytes_receivable_count = rx_bytes_available,
  };
  size_t crc_len = sizeof(local_status_out) - sizeof(local_status_out.crc);
  local_status_out.crc = crc32(CRC32_INIT, &local_status_out, crc_len);

  // Full duplex transaction to exchange Status:
  {
    spi_transfer_data transfer = {
      .wbuf = &local_status_out,
      .rbuf = &remote_status_in,
      .length = sizeof(SPITransportMsgStatus),
    };
    prv_do_blocking_spi_transfer(dev, &transfer);

    // Check the incoming CRC
    uint32_t crc = crc32(CRC32_INIT, &remote_status_in, sizeof(remote_status_in));
    if (crc != CRC32_RESIDUE) {
      PBL_LOG(LOG_LEVEL_ERROR, "CRC32 failed on Status Exchange: 0x%"PRIu32 " vs 0x%"PRIu32,
              crc, (uint32_t)CRC32_RESIDUE);
      PBL_LOG(LOG_LEVEL_DEBUG, "->OUT");
      PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)&local_status_out, sizeof(local_status_out));
      PBL_LOG(LOG_LEVEL_DEBUG, "->IN");
      PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)&remote_status_in, sizeof(remote_status_in));
      prv_core_dump();
    }

    HOST_TRANSPORT_DEBUG_LOG("Local Status: %u bytes sendable, %u bytes receivable",
                             local_status_out.bytes_sendable_count,
                             local_status_out.bytes_receivable_count);
    HOST_TRANSPORT_DEBUG_LOG("Remote Status: %u bytes sendable, %u bytes receivable",
                             remote_status_in.bytes_sendable_count,
                             remote_status_in.bytes_receivable_count);
  }

  // Single duplex write:
  size_t tx_len = MIN(tx_bytes_available, remote_status_in.bytes_receivable_count);
  if (tx_len) {
    // Calculate the CRC before Transmitting
    SPITransportMsgFooter tx_footer;
    tx_footer.crc = crc32(CRC32_INIT, tx_bytes, tx_len);

    HOST_TRANSPORT_DEBUG_LOG("Expecting to write %u bytes:", tx_len);
    spi_transfer_data transfer = {
      .wbuf = tx_bytes,
      .length = tx_len,
    };
    prv_do_blocking_spi_transfer(dev, &transfer);

    prv_lock();
    circular_buffer_consume(&s_tx_buffer, tx_len);
    prv_unlock();

    // Send the footer
    spi_transfer_data tx_footer_transfer = {
      .wbuf = &tx_footer,
      .length = sizeof(tx_footer),
    };

    prv_do_blocking_spi_transfer(dev, &tx_footer_transfer);

    HOST_TRANSPORT_DEBUG_LOG("Sent %u bytes.", tx_len);
  } else {
    HOST_TRANSPORT_DEBUG_LOG("Nothing to send.");
  }

  // Single duplex read:
  const size_t rx_len = MIN(remote_status_in.bytes_sendable_count, rx_bytes_available);
  if (rx_len) {
    HOST_TRANSPORT_DEBUG_LOG("Expecting to read %u bytes:", rx_len);
    spi_transfer_data transfer = {
      .rbuf = rx_buffer_ptr,
      .length = rx_len,
    };
    prv_do_blocking_spi_transfer(dev, &transfer);

    // Read CRC32 & confirm
    SPITransportMsgFooter rx_footer;
    spi_transfer_data rx_footer_transfer = {
      .rbuf = &rx_footer,
      .length = sizeof(rx_footer),
    };
    prv_do_blocking_spi_transfer(dev, &rx_footer_transfer);
    uint32_t crc = crc32(CRC32_INIT, rx_buffer_ptr, rx_len);
    if (crc != rx_footer.crc) {
      PBL_LOG(LOG_LEVEL_ERROR, "CRC32 failed on Data Read: 0x%"PRIu32 " vs 0x%"PRIu32,
              crc, rx_footer.crc);
      prv_core_dump();
    }
#if HOST_TRANSPORT_DEBUG
    HOST_TRANSPORT_DEBUG_LOG("Received %u bytes:", rx_len);
    PBL_HEXDUMP(LOG_LEVEL_DEBUG, rx_buffer_ptr, rx_len);
#endif
  } else {
    HOST_TRANSPORT_DEBUG_LOG("Nothing to receive.");
  }

  bool has_more_rx_data = (remote_status_in.bytes_sendable_count > rx_bytes_available);
  bool should_continue_to_rx_data =
      (has_more_rx_data && (local_status_out.bytes_receivable_count != 0));
  if (has_more_rx_data && !should_continue_to_rx_data) {
    HOST_TRANSPORT_DEBUG_LOG("Host Transport Receive Buffer Full, exiting from rx'ing to process");
  }

  prv_lock();
  circular_buffer_write_finish(&s_rx_buffer, rx_len);

  // Check if more data is available in the circular buffer.
  // If not, flip s_is_transacting back while the lock is taken. Otherwise, a concurrent call to
  // host_transport_tx_enqueue() would be prevented to unblock the transaction loop again.
  bool has_more_tx_data = circular_buffer_get_read_space_remaining(&s_tx_buffer);
  bool should_continue = (has_more_tx_data || should_continue_to_rx_data);
  s_is_transacting = should_continue;
  prv_unlock();

  return should_continue;
}

static void prv_host_transport_main(void *ctx) {
  static int8_t s_ble_host_transport_wdog_id;
  s_ble_host_transport_wdog_id = sys_watchdog_register(false);
  while (true) {
    sys_watchdog_notify(s_ble_host_transport_wdog_id);
    sys_watchdog_suspend(s_ble_host_transport_wdog_id);

    // Handle missed SPI CS edge:
    prv_sample_spi_cs_and_unblock_loop_if_needed();

    // Block the transaction loop until there's either data to transmit, or until the master
    // asserts the SCS line:
    xTaskNotifyWait(0, ~0, NULL, portMAX_DELAY);

    sys_watchdog_resume(s_ble_host_transport_wdog_id);

    HOST_TRANSPORT_DEBUG_LOG("prv_host_transport_main loop unblocked, about to read..");

    spi_device dev = ad_spi_open(PEBBLE_HOST);
    ad_spi_device_acquire(dev);

    host_transport_configure_spi_scs_pin(SCSPinFunction_SPI_CS);

    while (prv_transact(dev)) {};

    // Re-enable interrupt handling before processing,
    // so that endpoint handlers can cause the loop to get unblocked immediately:
    prv_reenable_spi_cs_wakeup_interrupt_handling();

    if (DialogTaskList[DialogTask_Ble] == 0) {
      // We don't bring up the ble task until we have received an init cmd
      hc_protocol_process_receive_buffer();
    } else {
      OS_TASK_NOTIFY(DialogTaskList[DialogTask_Ble], 0x0, eSetBits);
    }

    ad_spi_device_release(dev);
    ad_spi_close(dev);

    host_transport_configure_spi_scs_pin(SCSPinFunction_Wakeup_GPIO);
  }
}

HostTransportEnqueueStatus host_transport_tx_enqueue(const uint8_t *buffer, size_t length) {
  PBL_ASSERTN(length < HOST_TRANSPORT_CTLR_TX_BUFFER_SIZE &&
              length < HOST_TRANSPORT_HOST_RX_BUFFER_SIZE);

  prv_lock();
  bool success = circular_buffer_write(&s_tx_buffer, buffer, length);
  prv_unlock();

  if (success) {
    prv_disable_spi_cs_wakeup_interrupt_handling_and_unblock_transaction_loop();
    return HostTransportEnqueueStatus_Success;
  }

  PBL_LOG(LOG_LEVEL_DEBUG, "Failed to enqueue %u bytes", length);
  return HostTransportEnqueueStatus_RetryLater;
}

size_t host_transport_rx_get_length(void) {
  prv_lock();
  size_t rx_length = circular_buffer_get_read_space_remaining(&s_rx_buffer);
  prv_unlock();
  return rx_length;
}

bool host_transport_rx_read(uint8_t **data_ptr_out, size_t length) {
  prv_lock();
  bool caller_should_free = false;
  PBL_ASSERTN(circular_buffer_read_or_copy(&s_rx_buffer, data_ptr_out, length,
                                      kernel_malloc, &caller_should_free));
  prv_unlock();
  return caller_should_free;
}

void host_transport_rx_consume(size_t length) {
  prv_lock();
  circular_buffer_consume(&s_rx_buffer, length);
  prv_unlock();

  hc_protocol_buffer_gained_space();
}

bool host_transport_is_current_task_host_transport_task(void) {
  return (DialogTaskList[DialogTask_HostTrans] == xTaskGetCurrentTaskHandle());
}

// NB: used by core_dump.c
void host_transport_init_periph(void) {
  init_spi_pins();
}

static void prv_wakeup_init(void) {
  hw_wkup_init(NULL);
  hw_wkup_set_counter_threshold(1);
  hw_wkup_set_debounce_time(0);
  hw_wkup_configure_pin(HOST_SPI->spi.cs.port, HOST_SPI->spi.cs.pin,
                        true /* enabled */, HW_WKUP_PIN_STATE_LOW);
  hw_wkup_reset_interrupt();
  hw_wkup_reset_counter();
  hw_wkup_register_interrupt(prv_spi_chip_select_interrupt_handler, 1);
}

void host_transport_init(void) {
  SPI_BUS_INIT(SPI1);
  SPI_DEVICE_INIT(PEBBLE_HOST);

  circular_buffer_init(&s_tx_buffer, s_tx_storage, HOST_TRANSPORT_CTLR_TX_BUFFER_SIZE);
  circular_buffer_init(&s_rx_buffer, s_rx_storage, HOST_TRANSPORT_CTLR_RX_BUFFER_SIZE);

  // Start the task that runs the transaction loop:
  OS_BASE_TYPE status = OS_TASK_CREATE("HT", prv_host_transport_main,
                                       NULL /* ctx */, 1280 /* stack_size */,
                                       (tskIDLE_PRIORITY + 1) /* same as BLE task */,
                                       DialogTaskList[DialogTask_HostTrans]);
  PBL_ASSERTN(status == OS_TASK_CREATE_SUCCESS);

  prv_wakeup_init();

  host_transport_configure_spi_scs_pin(SCSPinFunction_Wakeup_GPIO);

  // Read SCS to see if it's already asserted by the MCU and we need to run the loop already:
  if (prv_is_scs_asserted()) {
    prv_disable_spi_cs_wakeup_interrupt_handling_and_unblock_transaction_loop();
  }

  hc_protocol_boot();
  hc_protocol_init();
  host_transport_ready = true;
}
