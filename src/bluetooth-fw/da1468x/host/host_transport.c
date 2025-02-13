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

#include "dialog_spi.h"
#include "hc_protocol/hc_protocol.h"
#include "hc_protocol_cb_handler.h"
#include "core_dump.h"
#include "drivers/periph_config.h"
#include "drivers/timer.h"
#include "kernel/pbl_malloc.h"
#include "kernel/pebble_tasks.h"
#include "kernel/util/sleep.h"
#include "kernel/util/stop.h"
#include "services/common/analytics/analytics.h"
#include "services/common/analytics/analytics_logging.h"
#include "services/common/bluetooth/bluetooth_ctl.h"
#include "services/common/system_task.h"
#include "system/hexdump.h"
#include "system/logging.h"
#include "system/passert.h"
#include "system/reset.h"
#include "util/assert.h"
#include "util/circular_buffer.h"
#include "util/crc32.h"
#include "util/math.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdint.h>

// Design doc & State Machine Diagram at:
// https://docs.google.com/document/d/1or2Ygs3sWt_5XNW_Mpe3Vxmhwuh3DTzdgZr6QlEe7iQ

// Core Dump doc at:
// https://docs.google.com/document/d/1UiasJyyQTD66mdpELfr7oLZjekOZn-o33E4iN5lQNic

// We want TIM6 to run at 32KHz
static const uint32_t TIMER_FREQUENCY_HZ = 32000;
// We want to be told when 500 ms elapses
static const uint32_t TIMER_PERIOD_TICKS = 16000;

typedef enum {
  SPITransportState_Idle,
  SPITransportState_StatusExchange,
  SPITransportState_WaitingForReceiving,
  SPITransportState_Receiving,
  SPITransportState_WaitingForReceiving_Footer,
  SPITransportState_Receiving_Footer,
  SPITransportState_WaitingForSending,
  SPITransportState_Sending,
  SPITransportState_WaitingForSending_Footer,
  SPITransportState_Sending_Footer,
  SPITransportState_CoreDump,
} SPITransportState;

typedef struct SPITransport {
  SPITransportState state;
  size_t bytes_receivable_count;
  size_t bytes_sendable_count;
  bool should_transact_after_consuming_rx;
  uint8_t *rx_write_ptr;
  const uint8_t *tx_read_ptr;
  CircularBuffer rx;
  CircularBuffer tx;
  SemaphoreHandle_t semph;
  TaskHandle_t task;
  volatile bool task_should_deinit;
  volatile bool task_is_running;
  SPITransportMsgStatus *status_local;
  SPITransportMsgStatus status_remote;
  uint8_t rx_storage[HOST_TRANSPORT_HOST_RX_BUFFER_SIZE];
  uint8_t *tx_storage;
  SPITransportMsgFooter rx_footer;
  SPITransportMsgFooter *tx_footer;
  bool watchdog_timer_active;
} SPITransport;

static uint8_t DMA_READ_BSS s_spi_tx_storage_buffer[HOST_TRANSPORT_HOST_TX_BUFFER_SIZE];
static SPITransportMsgFooter DMA_READ_BSS s_spi_tx_footer;
static SPITransportMsgStatus DMA_READ_BSS s_spi_status_local;

static SPITransport DMA_BSS s_spi_transport;
static bool s_is_host_transport_initialized;

static void prv_give_semamphore_from_isr(bool *should_context_switch);
static void prv_trigger_core_dump_from_isr(void);

static void prv_lock(void) {
  portENTER_CRITICAL();
}

static void prv_unlock(void) {
  portEXIT_CRITICAL();
}

static void prv_watchdog_init(void) {
  s_spi_transport.watchdog_timer_active = false;

  // Enable the timer clock
  periph_config_enable(BOARD_BT_WATCHDOG_TIMER.timer.peripheral,
                       BOARD_BT_WATCHDOG_TIMER.timer.config_clock);

  // Setup timer 3 to generate Bluetooth Host Transport priority interrupts
  NVIC_InitTypeDef NVIC_InitStructure;
  TIM_ClearITPendingBit(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, TIM_IT_Update);
  NVIC_InitStructure.NVIC_IRQChannel = BOARD_BT_WATCHDOG_TIMER.irq_channel;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = tskIDLE_PRIORITY + 3;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  // Setup timer 3 for periodic interrupts at TIMER_INTERRUPT_HZ
  TIM_TimeBaseInitTypeDef  tim_config;
  TIM_TimeBaseStructInit(&tim_config);
  tim_config.TIM_Prescaler = timer_find_prescaler(&BOARD_BT_WATCHDOG_TIMER.timer,
                                                  TIMER_FREQUENCY_HZ);
  tim_config.TIM_Period = TIMER_PERIOD_TICKS;
  tim_config.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, &tim_config);

  TIM_Cmd(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, DISABLE);
  TIM_ClearITPendingBit(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, TIM_IT_Update);
  TIM_ITConfig(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, TIM_IT_Update, ENABLE);
}

static void prv_watchdog_deinit(void) {
  TIM_Cmd(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, DISABLE);
  TIM_ITConfig(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, TIM_IT_Update, DISABLE);
  periph_config_disable(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, RCC_APB1Periph_TIM6);
  s_spi_transport.watchdog_timer_active = false;
}

static void prv_watchdog_start(void) {
  prv_lock();
  if (!s_spi_transport.watchdog_timer_active) {
    TIM_SetCounter(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, 0); // Reset the count to 0.
    TIM_Cmd(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, ENABLE);
    s_spi_transport.watchdog_timer_active = true;

    // Prevent us from entering stop mode (and disabling the clock timer)
    stop_mode_disable(InhibitorBluetoothWatchdog);
  }
  prv_unlock();
}

static void prv_watchdog_stop(void) {
  prv_lock();
  if (s_spi_transport.watchdog_timer_active) {
    TIM_Cmd(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, DISABLE);
    s_spi_transport.watchdog_timer_active = false;
    // Allow us to enter stop mode
    stop_mode_enable(InhibitorBluetoothWatchdog);
  }
  prv_unlock();
}

static void prv_trigger_core_dump_from_isr(void) {
  bool should_context_switch;
  s_spi_transport.state = SPITransportState_CoreDump;
  prv_give_semamphore_from_isr(&should_context_switch);
}

#if !defined(DIALOG_TIMER_IRQ_HANDLER)
#error "DIALOG_TIMER_IRQ_HANDLER must be defined in the board_*.h file!"
#endif
void DIALOG_TIMER_IRQ_HANDLER(void *ctx) {
  dbgserial_putstr("BT Trans TO!");

  prv_watchdog_stop();

  prv_trigger_core_dump_from_isr();

  TIM_ClearITPendingBit(BOARD_BT_WATCHDOG_TIMER.timer.peripheral, TIM_IT_Update);
}

static void prv_give_semamphore_from_isr(bool *should_context_switch) {
  portBASE_TYPE was_higher_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(s_spi_transport.semph, &was_higher_task_woken);
  *should_context_switch = (was_higher_task_woken != pdFALSE);
}

static void prv_handle_sending_footer_complete_isr(bool *should_context_switch) {
  s_spi_transport.state = SPITransportState_Idle;
  prv_lock();
  circular_buffer_consume(&s_spi_transport.tx, s_spi_transport.bytes_sendable_count);
  prv_unlock();

  prv_give_semamphore_from_isr(should_context_switch);
}

static void prv_handle_sending_complete_isr(bool *should_context_switch) {
  // Wait for the controller to signal that it's ready
  prv_watchdog_start();
  s_spi_transport.state = SPITransportState_WaitingForSending_Footer;
}

static void prv_handle_int_waiting_for_sending_footer(void) {
  s_spi_transport.state = SPITransportState_Sending_Footer;
  s_spi_transport.tx_footer->crc = crc32(
      CRC32_INIT, s_spi_transport.tx_read_ptr,
      s_spi_transport.bytes_sendable_count);

  dialog_spi_send_and_receive_dma(s_spi_transport.tx_footer, NULL,
                                  sizeof(*s_spi_transport.tx_footer),
                                  prv_handle_sending_footer_complete_isr);
}

static void prv_handle_int_waiting_for_sending(void) {
  s_spi_transport.state = SPITransportState_Sending;
  dialog_spi_send_and_receive_dma(s_spi_transport.tx_read_ptr, NULL,
                                  s_spi_transport.bytes_sendable_count,
                                  prv_handle_sending_complete_isr);
}

static void prv_handle_circular_buffer_write_complete(void) {
  prv_lock();
  circular_buffer_write_finish(&s_spi_transport.rx, s_spi_transport.bytes_receivable_count);
  prv_unlock();
}

static void prv_handle_receiving_footer_complete_isr(bool *should_context_switch) {
  // Check the received CRC
  uint32_t crc = crc32(CRC32_INIT, s_spi_transport.rx_write_ptr,
                       s_spi_transport.bytes_receivable_count);
  if (crc != s_spi_transport.rx_footer.crc) {
    PBL_LOG(LOG_LEVEL_ERROR, "CRC failed on remote SPI Receive 0x%08" PRIX32 " != 0x%08" PRIX32,
            crc, s_spi_transport.rx_footer.crc);

    prv_trigger_core_dump_from_isr();
  }

  bool should_give_semaphore = false;
  if (s_spi_transport.bytes_sendable_count) {
    prv_watchdog_start();
    s_spi_transport.state = SPITransportState_WaitingForSending;
  } else {
    // Nothing to send, we're done:
    s_spi_transport.state = SPITransportState_Idle;
    should_give_semaphore = true;
  }

  prv_handle_circular_buffer_write_complete();

  if (should_give_semaphore) {
    prv_give_semamphore_from_isr(should_context_switch);
  }
}

static void prv_handle_receiving_complete_isr(bool *should_context_switch) {
  // Wait for the controller to signal that it's ready
  prv_watchdog_start();
  s_spi_transport.state = SPITransportState_WaitingForReceiving_Footer;
}

static void prv_handle_int_waiting_for_receiving(void) {
  s_spi_transport.state = SPITransportState_Receiving;
  dialog_spi_send_and_receive_dma(NULL, s_spi_transport.rx_write_ptr,
                                  s_spi_transport.bytes_receivable_count,
                                  prv_handle_receiving_complete_isr);
}

static void prv_handle_int_waiting_for_receiving_footer(void) {
  s_spi_transport.state = SPITransportState_Receiving_Footer;
  dialog_spi_send_and_receive_dma(NULL, &s_spi_transport.rx_footer,
                                  sizeof(s_spi_transport.rx_footer),
                                  prv_handle_receiving_footer_complete_isr);
}

static void prv_handle_status_exchange_complete_isr(bool *should_context_switch) {
  uint32_t crc = crc32(CRC32_INIT, &s_spi_transport.status_remote,
                       sizeof(s_spi_transport.status_remote));
  if (crc != CRC32_RESIDUE) {
    PBL_LOG(LOG_LEVEL_ERROR, "CRC failed on remote SPITransportMsgStatus 0x%"PRIx32 " vs 0x%"PRIx32,
            crc, (uint32_t)CRC32_RESIDUE);
    PBL_LOG(LOG_LEVEL_DEBUG, "->OUT");
    PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)s_spi_transport.status_local,
                sizeof(s_spi_transport.status_remote));
    PBL_LOG(LOG_LEVEL_DEBUG, "->IN:");
    PBL_HEXDUMP(LOG_LEVEL_DEBUG, (uint8_t *)&s_spi_transport.status_remote,
                sizeof(s_spi_transport.status_remote));
    prv_trigger_core_dump_from_isr();
    return;
  }

  if (s_spi_transport.status_remote.msg_id != SPITransportMsgID_Status) {
    // Unexpected msg ID...
    prv_trigger_core_dump_from_isr();
    return;
  }

  // We now know how many bytes to receive / send:
  s_spi_transport.bytes_receivable_count = MIN(s_spi_transport.status_local->bytes_receivable_count,
                                               s_spi_transport.status_remote.bytes_sendable_count);
  s_spi_transport.bytes_sendable_count = MIN(s_spi_transport.status_local->bytes_sendable_count,
                                             s_spi_transport.status_remote.bytes_receivable_count);

  if (s_spi_transport.bytes_receivable_count == 0) {
    // There's no data to receive so close out the write prep to the rx buffer
    prv_handle_circular_buffer_write_complete();
  }

  // Nothing to do for now, just wait for INT to be asserted again.

  if (s_spi_transport.bytes_receivable_count) {
    prv_watchdog_start();
    s_spi_transport.state = SPITransportState_WaitingForReceiving;
  } else if (s_spi_transport.bytes_sendable_count) {
    prv_watchdog_start();
    s_spi_transport.state = SPITransportState_WaitingForSending;
  } else {
    // Nothing to send nor receive, back to idle:
    s_spi_transport.state = SPITransportState_Idle;
  }
}

static void prv_update_local_status_msg(void) {
  prv_lock();
  circular_buffer_read(&s_spi_transport.tx, s_spi_transport.tx.data_length,
                       &s_spi_transport.tx_read_ptr,
                       &s_spi_transport.status_local->bytes_sendable_count);
  s_spi_transport.status_local->bytes_receivable_count =
      circular_buffer_write_prepare(&s_spi_transport.rx, &s_spi_transport.rx_write_ptr);
  prv_unlock();

  size_t crc_len = sizeof(*s_spi_transport.status_local) -
                   sizeof(s_spi_transport.status_local->crc);
  s_spi_transport.status_local->crc = crc32(CRC32_INIT, s_spi_transport.status_local, crc_len);
}

static void prv_handle_int_idle(void) {
  // We're idle. First do the "status exchange", so we know how much to send & receive.
  s_spi_transport.state = SPITransportState_StatusExchange;
  prv_update_local_status_msg();
  dialog_spi_send_and_receive_dma(s_spi_transport.status_local,
                                  &s_spi_transport.status_remote,
                                  sizeof(SPITransportMsgStatus),
                                  prv_handle_status_exchange_complete_isr);
}

static bool prv_handle_rx_buffer_full(void) {
  prv_lock();
  bool is_rx_buffer_full = (0 == circular_buffer_get_write_space_remaining(&s_spi_transport.rx));
  if (is_rx_buffer_full) {
    s_spi_transport.should_transact_after_consuming_rx = true;
  }
  prv_unlock();
  return is_rx_buffer_full;
}

static void prv_handle_rx_buffer_has_space_available(void) {
  // The slave is waiting to send more, just fire the external interrupt again:
  if (s_spi_transport.should_transact_after_consuming_rx) {
    s_spi_transport.should_transact_after_consuming_rx = false;
    dialog_spi_set_pending_int();
  }
}

static void prv_wtf_analytic_cb(void *context) {
#ifndef RECOVERY_FW
  ANALYTICS_LOG_DEBUG("Bluetooth Host Transport WTF");
  AnalyticsEventBlob event_blob = {
    .event = AnalyticsEvent_BtLockupError,
  };
  analytics_logging_log_event(&event_blob);
#endif
}

static void prv_int_exti_cb(bool *should_context_switch) {
  // The controller responded -- stop the watchdog.
  prv_watchdog_stop();

  switch (s_spi_transport.state) {
    case SPITransportState_Idle:
      // If the RX buffer is full, defer ack'ing the Dialog INT until we have
      // some room to actually receive data.
      //
      // Note: We could probably skip this check altogether but I think it will
      // prevent the BT controller from continously doing a status exchange
      // only to find out it can't send any data
      if (!prv_handle_rx_buffer_full()) {
        prv_handle_int_idle();
      }
      break;

    case SPITransportState_WaitingForReceiving:
      prv_handle_int_waiting_for_receiving();
      break;

    case SPITransportState_WaitingForReceiving_Footer:
      prv_handle_int_waiting_for_receiving_footer();
      break;

    case SPITransportState_WaitingForSending:
      prv_handle_int_waiting_for_sending();
      break;

    case SPITransportState_WaitingForSending_Footer:
      prv_handle_int_waiting_for_sending_footer();
      break;

    case SPITransportState_StatusExchange:
    case SPITransportState_Receiving:
    case SPITransportState_Sending:
    case SPITransportState_Receiving_Footer:
    case SPITransportState_Sending_Footer: {
      // Got INT while DMA TC interrupt hasn't happened yet.
      // This might happen if the Dialog side would be *extremely* fast and assert the INT line
      // faster than the DMA transfer complete interrupt happens. Probably very unlikely, but
      // in case we're worried about this:
      // FIXME: Maybe the solution is to disable the EXTI while DMA is on-going and re-enable
      // when it's finished? OTOH, this might be tricky when we're also using INT toggles to
      // detect a spontaneous reset of the Dialog chip.

      // For now, let's try to collect a core dump and then cleanly restart BLE. The root cause
      // will be determined later.
      // Also, collect an analytic to see how frequently this happens.
      bool should_context_switch;
      system_task_add_callback_from_isr(prv_wtf_analytic_cb, NULL, &should_context_switch);

      prv_trigger_core_dump_from_isr();
      break;
    }

    case SPITransportState_CoreDump:
      // Do nothing. The proper authorities have already been notified.
      break;

    default:
      break;
  }
}

static size_t prv_host_transport_tx_get_length(void) {
  prv_lock();
  size_t rx_length = circular_buffer_get_read_space_remaining(&s_spi_transport.tx);
  prv_unlock();
  return rx_length;
}

static void prv_indicate_has_data_if_idle(void) {
  prv_lock();
  bool is_idle = (s_spi_transport.state == SPITransportState_Idle);
  prv_unlock();
  if (is_idle) {
    prv_watchdog_start();
    dialog_spi_indicate_data_ready_to_tx();
  }
}

static void prv_host_transport_main(void *unused) {
  s_spi_transport.task_is_running = true;

  while (true) {
    xSemaphoreTake(s_spi_transport.semph, portMAX_DELAY);

    if (s_spi_transport.task_should_deinit) {
      break;
    }
    if (s_spi_transport.state == SPITransportState_CoreDump) {
      PBL_LOG(LOG_LEVEL_ALWAYS, "Bluetooth Host Transport Crash -- Dumping Core");
      core_dump_and_reset_or_reboot();
      break;
    }

    // It's possible new data has been batched up while we were sending data.
    // Check the circular buffer and notify the controller if that's the case
    if (prv_host_transport_tx_get_length() > 0) {
      prv_indicate_has_data_if_idle();
    }

    hc_protocol_process_receive_buffer();

    prv_handle_rx_buffer_has_space_available();
  }

  // Clean-up & kill this task
  pebble_task_unregister(PebbleTask_BTRX);

  // Signal host_transport_deinit() that we're done
  s_spi_transport.task_is_running = false;
  vTaskDelete(NULL);
}

HostTransportEnqueueStatus host_transport_tx_enqueue(const uint8_t *data, size_t length) {
  if (!s_is_host_transport_initialized) {
    return HostTransportEnqueueStatus_Failure;
  }

  if (s_spi_transport.state == SPITransportState_CoreDump) {
    PBL_LOG(LOG_LEVEL_INFO, "Skipping host transport enqueue .. waiting for reset");
    return HostTransportEnqueueStatus_Failure;
  }

  PBL_ASSERTN(length < HOST_TRANSPORT_HOST_TX_BUFFER_SIZE &&
              length < HOST_TRANSPORT_CTLR_RX_BUFFER_SIZE);

  prv_lock();
  bool success = circular_buffer_write(&s_spi_transport.tx, data, length);
  prv_unlock();

  if (success) {
    prv_indicate_has_data_if_idle();
    analytics_add(ANALYTICS_DEVICE_METRIC_BT_UART_BYTES_OUT, length, AnalyticsClient_System);
    return HostTransportEnqueueStatus_Success;
  }

  return HostTransportEnqueueStatus_RetryLater;
}

size_t host_transport_rx_get_length(void) {
  prv_lock();
  size_t rx_length = circular_buffer_get_read_space_remaining(&s_spi_transport.rx);
  prv_unlock();
  return rx_length;
}

bool host_transport_rx_read(uint8_t **data_ptr_out, size_t length) {
  prv_lock();
  bool caller_should_free = false;
  PBL_ASSERTN(circular_buffer_read_or_copy(&s_spi_transport.rx, data_ptr_out, length,
                                           kernel_malloc, &caller_should_free));
  prv_unlock();
  return caller_should_free;
}

void host_transport_rx_consume(size_t length) {
  prv_lock();
  circular_buffer_consume(&s_spi_transport.rx, length);
  prv_unlock();

  hc_protocol_buffer_gained_space();
  analytics_add(ANALYTICS_DEVICE_METRIC_BT_UART_BYTES_IN, length, AnalyticsClient_System);
}

bool host_transport_is_current_task_host_transport_task(void) {
  return (s_spi_transport.task == xTaskGetCurrentTaskHandle());
}

bool host_transport_init(void) {
  s_spi_transport.tx_storage = s_spi_tx_storage_buffer;
  s_spi_transport.tx_footer = &s_spi_tx_footer;
  s_spi_transport.status_local = &s_spi_status_local;
  s_spi_transport.semph = xSemaphoreCreateBinary();
  PBL_ASSERTN(s_spi_transport.semph);

  prv_watchdog_init();

  hc_protocol_cb_dispatcher_init();

  s_spi_transport.task_is_running = false;
  s_spi_transport.task_should_deinit = false;

  TaskParameters_t task_params = {
    .pvTaskCode = prv_host_transport_main,
    .pcName = "BTTrans",
    .usStackDepth = configMINIMAL_STACK_SIZE * 2,
    .uxPriority = (tskIDLE_PRIORITY + 3) | portPRIVILEGE_BIT,
    .puxStackBuffer = NULL, // TODO: Do we want the stack to live in a particular place?
  };
  pebble_task_create(PebbleTask_BTRX, &task_params, &s_spi_transport.task);

  s_spi_transport.state = SPITransportState_Idle;

  s_spi_transport.should_transact_after_consuming_rx = false;

  *s_spi_transport.status_local = (SPITransportMsgStatus) {};
  s_spi_transport.status_local->msg_id = SPITransportMsgID_Status;

  circular_buffer_init(&s_spi_transport.rx, s_spi_transport.rx_storage,
                       HOST_TRANSPORT_HOST_RX_BUFFER_SIZE);
  circular_buffer_init(&s_spi_transport.tx, s_spi_transport.tx_storage,
                       HOST_TRANSPORT_HOST_TX_BUFFER_SIZE);
  dialog_spi_init(prv_int_exti_cb);

  s_is_host_transport_initialized = true;

  return true;
}

void host_transport_deinit(void) {
  PBL_LOG(LOG_LEVEL_DEBUG, "BLE transport deinit");
  s_is_host_transport_initialized = false;

  prv_watchdog_deinit();

  dialog_spi_deinit();

  // Flag the task exit, wake the task, and wait for the task to exit before continuing
  s_spi_transport.task_should_deinit = true;
  xSemaphoreGive(s_spi_transport.semph);
  while (s_spi_transport.task_is_running) {
    // Trigger context switches & give low priority tasks a chance to run until
    // the host_transport thread wakes and exits
    psleep(2);
  }
  vSemaphoreDelete(s_spi_transport.semph);
  s_spi_transport.semph = NULL;

  s_spi_transport.task = NULL;

  hc_protocol_cb_dispatcher_deinit();
}
