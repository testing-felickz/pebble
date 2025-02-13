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

#include "system/logging.h"
#include "custom_config_main.h"
#include "tasks.h"
#include "hc_protocol/hc_endpoint_logging.h"

#include <logging/binary_logging.h>
#include <pebbleos/chip_id.h>

#include "kernel/pbl_malloc.h"
#include <mcu/interrupts.h>
#include <util/circular_buffer.h>
#include <util/crc32.h>

#include <hw_uart.h>
#include <hw_gpio.h>
#include <osal.h>
#include <sys_power_mgr.h>

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// See pbl_log_init()
extern const uint8_t __ble_vars_start__;

_Static_assert((CORE_ID_BLE & PACKED_CORE_MASK) == CORE_ID_BLE, "Core number invalid");
#define str(s) xstr(s)
#define xstr(s) #s


#define MAX_MSG_LEN (128)
#define MAX_MSG_STR_LEN (MAX_MSG_LEN - sizeof(BinLogMessage_Param_v1) - (9 * sizeof(uint32_t)))
#define MAX_MSG_STR_LEN_HALF (MAX_MSG_STR_LEN / 2)

// Gives us ~13ms of message flush latency.
#define LOG_BUFFER_SIZE (MAX_MSG_LEN * 3)

#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

#define NEW_LOG_HEADER "NL" NEW_LOG_VERSION

// Define the .log_string section format.
static const char prv_NewLogHeader[] __attribute__((nocommon, used, section(".log_string.header")))
    = NEW_LOG_HEADER "=<file>:<line>:<level>:<color>:<msg>," \
                     "CORE_ID=" str(CORE_ID_BLE) ",CORE_NAME=da1468x";

// Confirm the size calculations. If these fail, update tools/loghashing/check_elf_log_strings.py
// We can't currently handle 64 bit values.
_Static_assert(sizeof(long int)  <= 4, "long int larger than expected");
_Static_assert(sizeof(size_t)    <= 4, "size_t larger than expected");
_Static_assert(sizeof(ptrdiff_t) <= 4, "ptrdiff_t larger than expected");


static void prv_start_tx(void);
void debug_uart_init(void);

// LogBuffer -- Circular buffer & control for the log hashing output buffer
typedef struct LogBuffer {
  CircularBuffer circular_buffer;
  uint8_t level;
  volatile bool transmitting;
} LogBuffer;

static uint8_t s_log_buffer_ram[LOG_BUFFER_SIZE] __attribute__((section(".log_buffer")));
static LogBuffer s_log_buffer = { .level = LOG_LEVEL_ALWAYS, .transmitting = false, };

/*
 * Duplicate _VERBOSE -- let's have a reasonable entry for every value should something go
 * wrong on the packing end.
 */
static const uint8_t LEVEL_MAP[8] = { LOG_LEVEL_ALWAYS, LOG_LEVEL_ERROR, LOG_LEVEL_WARNING,
                                      LOG_LEVEL_INFO, LOG_LEVEL_DEBUG, LOG_LEVEL_DEBUG_VERBOSE,
                                      LOG_LEVEL_DEBUG_VERBOSE, LOG_LEVEL_DEBUG_VERBOSE };
static const struct OVERRUN {
  HcProtocolMessage hc_header;
  BinLogMessage_Header_v1 header;
  uint8_t string[8];
} OVERRUN = {
      .hc_header = { .message_length = sizeof(OVERRUN), },
      .header = { .version = BINLOGMSG_VERSION_STRING_V1, .length = 8, },
      .string = "OVERRUN\0",
};


void pbl_log_set_level(uint8_t level) {
  s_log_buffer.level = level;
}

uint8_t pbl_log_get_level(void) {
  return s_log_buffer.level;
}

int printf(const char *__restrict format, ...) {
  char line_buffer[128];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(line_buffer, sizeof(line_buffer), format, args);
  hw_uart_write_buffer(CONFIG_LOG_UART,
                       line_buffer, MIN((int)sizeof(line_buffer), (unsigned int)len));
  va_end(args);
  return 0;
}

static void prv_lock(void) {
  OS_ENTER_CRITICAL_SECTION();
}

static void prv_unlock(void) {
  OS_LEAVE_CRITICAL_SECTION();
}

static bool prv_pm_prepare_for_sleep(void) {
  /* Do not sleep when there is transmission in progress */
  return !hw_uart_tx_in_progress(CONFIG_LOG_UART);
}

static void prv_pm_sleep_canceled(void) {
}

static void prv_pm_wake_up_ind(bool arg) {
}

static void prv_pm_xtal16m_ready_ind(void) {
  debug_uart_init();
}

// Called by Core Dump
void debug_uart_init(void) {
  const uart_config uart_init = {
          .baud_rate = CONFIG_LOG_UART_BAUDRATE,
          .data      = CONFIG_LOG_UART_DATABITS,
          .stop      = CONFIG_LOG_UART_STOPBITS,
          .parity    = CONFIG_LOG_UART_PARITY,
          .use_fifo  = 1,
          .rx_dma_channel = CONFIG_LOG_UART_RX_DMA_CHANNEL,
          .tx_dma_channel = CONFIG_LOG_UART_TX_DMA_CHANNEL,
  };
  hw_uart_init(CONFIG_LOG_UART, &uart_init);

  hw_gpio_set_pin_function(HW_GPIO_PORT_1, HW_GPIO_PIN_0, HW_GPIO_MODE_OUTPUT,
                           HW_GPIO_FUNC_UART_RX);
  hw_gpio_set_pin_function(HW_GPIO_PORT_1, HW_GPIO_PIN_1, HW_GPIO_MODE_OUTPUT,
                           HW_GPIO_FUNC_UART_TX);
}

void pbl_log_init(void) {
  debug_uart_init();

  // Let's use all available RAM for the log buffer!
  // That is, the difference between s_log_buffer_ram and RETENTION_BLE (__ble_vars_start__).
  // This will always be at least sizeof(s_log_buffer_ram).
  uint32_t start_addr = (uint32_t)s_log_buffer_ram;
  uint32_t end_addr = (uint32_t)&__ble_vars_start__;
  uint32_t available_ram = end_addr - start_addr;

  uint16_t circular_buffer_size = sizeof(s_log_buffer_ram);
  if (circular_buffer_size < available_ram) {
    circular_buffer_size = MIN(available_ram, USHRT_MAX);
  }

  circular_buffer_init_ex(&s_log_buffer.circular_buffer,
                          s_log_buffer_ram, circular_buffer_size,
                          false /* auto_reset */); // Leave old data in the buffer for later debug

  static const adapter_call_backs_t s_ad_uart_pm_call_backs = {
    .ad_prepare_for_sleep = prv_pm_prepare_for_sleep,
    .ad_sleep_canceled = prv_pm_sleep_canceled,
    .ad_wake_up_ind = prv_pm_wake_up_ind,
    .ad_xtal16m_ready_ind = prv_pm_xtal16m_ready_ind,
    .ad_sleep_preparation_time = 0
  };

  pm_register_adapter(&s_ad_uart_pm_call_backs);
}

static void prv_uart_tx_callback(void *user_data, uint16_t written) {
  prv_lock();
  s_log_buffer.transmitting = false;
  circular_buffer_consume(&s_log_buffer.circular_buffer, written);

  // Attempt to kick off another transmit
  prv_start_tx();
  prv_unlock();
}

//! Must be in a critical section (interrupts off) when this function is called!
static void prv_start_tx(void) {
  const uint8_t *data;
  uint16_t length;

  // Don't start another TX if we're already writing
  if (s_log_buffer.transmitting) {
    return;
  }

  // How much is available to transmit?
  length = circular_buffer_get_read_space_remaining(&s_log_buffer.circular_buffer);

  if (length == 0) {
    return;
  }

  s_log_buffer.transmitting = true;
  circular_buffer_read(&s_log_buffer.circular_buffer, length, &data, &length);
  hw_uart_send(CONFIG_LOG_UART, data, length, prv_uart_tx_callback, NULL);
}

static void prv_log_to_buffer(uint8_t *buffer, uint16_t length) {
  prv_lock();
  if (!circular_buffer_write(&s_log_buffer.circular_buffer, buffer, length)) {
    // Out of memory! Attempt to print an overrun message
    circular_buffer_write(&s_log_buffer.circular_buffer, (uint8_t *)&OVERRUN.header,
                          sizeof(OVERRUN) - sizeof(HcProtocolMessage));
  }
  prv_start_tx();
  prv_unlock();
}

#ifdef PBL_LOGS_HASHED

void pbl_log_hashed(const uint32_t packed_loghash, ...) {
  unsigned num_fmt_conversions = (packed_loghash >> PACKED_NUM_FMT_OFFSET) & PACKED_NUM_FMT_MASK;
  unsigned str_index_1 = (packed_loghash >> PACKED_STR1FMT_OFFSET) & PACKED_STR1FMT_MASK;
  unsigned str_index_2 = (packed_loghash >> PACKED_STR2FMT_OFFSET) & PACKED_STR2FMT_MASK;
  unsigned level = (packed_loghash >> PACKED_LEVEL_OFFSET) & PACKED_LEVEL_MASK;
  unsigned hash = (packed_loghash & PACKED_HASH_MASK) |
                   ((CORE_ID_BLE & PACKED_CORE_MASK) << PACKED_CORE_OFFSET);

  // Calculate the total message size. This is more complicated if there are strings
  // Start with the header and the number of parameters.
  unsigned num_strings = (str_index_1 ? 1 : 0) + (str_index_2 ? 1 : 0);
  unsigned msg_size = sizeof(BinLogMessage_Param_v1) +
                      (num_fmt_conversions - num_strings) * sizeof(uint32_t);

  unsigned str1_len = 0;
  unsigned str2_len = 0;

  // Now, count the string size, if necessary
  if (num_strings) {
    va_list args;
    va_start(args, packed_loghash);
    for (unsigned index = 0; index < num_fmt_conversions; ++index) {
      const char *str = (const char *)va_arg(args, const char *);
      if ((index + 1 == str_index_1) || (index + 1 == str_index_2)) {
        unsigned len = strlen(str) + 1; // TODO: remove NULL termination
        if ((index + 1) == str_index_1) {
          str1_len = len;
        } else {
          str2_len = len;
        }
      }
    }
    va_end(args);

    // Make sure the strings don't blow our max message setting
    if ((str1_len + str2_len) >= MAX_MSG_STR_LEN) {
      str1_len = MIN(str1_len, MAX_MSG_STR_LEN_HALF);
      str2_len = MIN(str2_len, MAX_MSG_STR_LEN_HALF);
    }

    if (str1_len) {
      // length + strlen(string) + padding (round up to nearest uint32_t).
      msg_size += 1 + str1_len + (3 - (1 + str1_len + 3) % 4);
    }
    if (str2_len) {
      // length + strlen(string) + padding (round up to nearest uint32_t).
      msg_size += 1 + str2_len + (3 - (1 + str2_len + 3) % 4);
    }
  }

  // Buffer for UART ASCII log output
  char expanded_fmt_buffer[64];
  memset(expanded_fmt_buffer, 0, sizeof(expanded_fmt_buffer));

  // Craft a buffer that will hold HcProtocolMessage followed by BinLogMessage.
  int msg_buffer_size = MAX((msg_size + sizeof(HcProtocolMessage)), 130);
  uint8_t msg_buffer[msg_buffer_size];
  memset(msg_buffer, 0, msg_buffer_size);

  HcProtocolMessage *hc_msg = (HcProtocolMessage *)&msg_buffer[0];
  BinLogMessage_Param_v1 *msg = (BinLogMessage_Param_v1 *)&msg_buffer[sizeof(HcProtocolMessage)];

  msg->header.version = BINLOGMSG_VERSION_PARAM_V1;
  msg->header.length = msg_size;
  msg->header.time.millisecond = LEVEL_MAP[level]; // HACK: Carry the level in the millisecond field
  // msg->header.date & msg->header.time will be filled in on the host side

  // Set the message ID. Use as much as possible directly from the hash, then set the core & task.
  msg->body.msgid.msg_id = (packed_loghash & MSGID_STR_AND_HASH_MASK);
  msg->body.msgid.core_number = CORE_ID_BLE;
  msg->body.msgid.task_id = task_get_dialogtask();

  uint32_t *param = msg->body.payload;

  va_list args;
  va_start(args, packed_loghash);
  for (unsigned index = 0; index < num_fmt_conversions; ++index) {
    if ((index + 1 == str_index_1) || (index + 1 == str_index_2)) {
      BinLogMessage_StringParam *str_param = (BinLogMessage_StringParam *)param;
      const char *str = (const char *)va_arg(args, const char *);
      int str_len = ((index + 1) == str_index_1) ? str1_len : str2_len;
      str_param->length = str_len;

      // TODO remove NULL termination
      memcpy(str_param->string, str, str_len - 1);
      str_param->string[str_len - 1] = '\0';

      param += (str_len + 1 + 3) / sizeof(uint32_t);

      // For UART Output
      strcat(expanded_fmt_buffer, " `%s`");
    } else {
      *param++ = (uint32_t)va_arg(args, uint32_t);

      // For UART Output
      strcat(expanded_fmt_buffer, " %x");
    }
  }
  va_end(args);

  // Log to host if the level is sufficient
  if (LEVEL_MAP[level] <= s_log_buffer.level) {
    // Craft the HcProtocolMessage & send to the host
    hc_msg->message_length = sizeof(HcProtocolMessage) + msg->header.length;
    if (!hc_endpoint_logging_send_msg(hc_msg)) {
      // Attempt to send an OVERRUN message.
      hc_endpoint_logging_send_msg((HcProtocolMessage *)&OVERRUN.hc_header);
    }
  }


  // Re-use the msg_buffer.
  va_start(args, packed_loghash);
  int header_length = snprintf((char *)msg_buffer, msg_buffer_size, ":0> NL:%x", hash);
  int length = vsnprintf((char *)msg_buffer + header_length, msg_buffer_size - header_length,
                         expanded_fmt_buffer, args);
  length += header_length;
  va_end(args);

  if ((length + 2) >= msg_buffer_size) {
    length -= 2;
  }
  msg_buffer[length++] = '\r';
  msg_buffer[length++] = '\n';

  prv_log_to_buffer((uint8_t *)msg_buffer, length);
}

#else // PBL_LOGS_HASHED

static char prv_get_log_level_char(uint8_t log_level) {
  switch (log_level) {
    case LOG_LEVEL_ALWAYS: return 'A';
    case LOG_LEVEL_ERROR: return 'E';
    case LOG_LEVEL_WARNING: return 'W';
    case LOG_LEVEL_INFO: return 'I';
    case LOG_LEVEL_DEBUG: return 'D';
    case LOG_LEVEL_DEBUG_VERBOSE: return 'V';
    default: return '?';
  }
}

void pbl_log(uint8_t log_level, const char *src_filename, int src_line_number,
             const char *fmt, ...) {
  // Craft a buffer that will hold HcProtocolMessage followed by BinLogMessage.
  unsigned int msg_buffer_size = MAX((MAX_MSG_LEN + sizeof(HcProtocolMessage) + 40), 130);
  uint8_t msg_buffer[msg_buffer_size];
  memset(msg_buffer, 0, msg_buffer_size);

  HcProtocolMessage *hc_msg = (HcProtocolMessage *)&msg_buffer[0];
  BinLogMessage_Unhashed_v1 *msg = (BinLogMessage_Unhashed_v1 *)
                                      &msg_buffer[sizeof(HcProtocolMessage)];

  msg->header.version = BINLOGMSG_VERSION_UNHASHED_V1;
  // HACK: store the log level in time.millisecond
  msg->header.time.millisecond = log_level;
  // msg->header.date & msg->header.time will be filled in on the host side

  msg->body.line_number = src_line_number;
  strncpy((char *)msg->body.filename, src_filename,
          MEMBER_SIZE(BinLogMessage_UnhashedBody, filename));
  msg->body.core_number = CORE_ID_BLE;
  msg->body.task_id = task_get_dialogtask();
  msg->body.level = log_level;

  // Temporarily reserve space for a NULL terminator
  // TODO: remove NULL termination
  uint8_t max_str_len = msg_buffer_size - sizeof(BinLogMessage_Unhashed_v1) - 1;

  va_list args;
  va_start(args, fmt);
  msg->body.length = vsnprintf((char *)msg->body.string, max_str_len, fmt, args);
  va_end(args);

  // Temporarily force NULL termination. The buffer is already set to zero -- increment the length
  // by one to reflect the increased string length
  // TODO: remove NULL termination
  msg->body.length++;

  // Calculate the total packet length.
  msg->header.length = sizeof(BinLogMessage_Unhashed_v1) + msg->body.length;
  // Rounding up to the nearest uint32_t
  msg->header.length = (msg->header.length + (sizeof(uint32_t) - 1)) & ~(sizeof(uint32_t) - 1);

  // Log to host if the level is sufficient
  if (LEVEL_MAP[log_level] <= s_log_buffer.level) {
    // Craft the HcProtocolMessage & send to the Host.
    hc_msg->message_length = sizeof(HcProtocolMessage) + msg->header.length;
    if (!hc_endpoint_logging_send_msg(hc_msg)) {
      // Attempt to send an OVERRUN message.
      hc_endpoint_logging_send_msg((HcProtocolMessage *)&OVERRUN.hc_header);
    }
  }

  // Send the unhashed string to the UART buffer
  size_t filename_max_len = MEMBER_SIZE(BinLogMessage_UnhashedBody, filename);
  size_t src_filename_len = strnlen((const char *)msg->body.filename, filename_max_len);
  size_t pad_length = (src_filename_len < filename_max_len) ?
      (filename_max_len - src_filename_len) : 1;

  // Re-use the msg_buffer.
  int length = snprintf((char *)msg_buffer, msg_buffer_size, "%c %d %*c%.*s:%3i> %.*s",
                        prv_get_log_level_char(msg->body.level), msg->body.task_id,
                        pad_length, ' ', filename_max_len, msg->body.filename,
                        msg->body.line_number, msg->body.length, msg->body.string);

  const unsigned length_with_null = length + 1;
  if (length_with_null >= msg_buffer_size) {
    length--;
  }
  msg_buffer[length++] = '\n';

  prv_log_to_buffer((uint8_t *)msg_buffer, length);
}

#endif // PBL_LOGS_HASHED
