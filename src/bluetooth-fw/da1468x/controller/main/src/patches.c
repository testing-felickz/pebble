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

#include "system/passert.h"
#include "system/logging.h"

// Dialog SDK:
#include "ble_common.h"
#include "dbg_swdiag.h"
#include "gapc.h"
#include "ke_timer.h"
#include "llc.h"
#include "llc_data.h"
#include "llc_util.h"
#include "lld_data.h"
#include "llm_util.h"

#include <util/size.h>

#include <stdint.h>
#include <string.h>

#define IS_AE_CHIP ((dg_configBLACK_ORCA_IC_REV == BLACK_ORCA_IC_REV_A) && \
                    (dg_configBLACK_ORCA_IC_STEP == BLACK_ORCA_IC_STEP_E))

//! Defined in sdk/interfaces/ble_stack/(Debug|Release)/libble_stack.a
//! Also see https://github.com/pebble/bt-dialog-source/blob/master/ble_stack/src/rom_patch/rom_patch.c#L2687
extern ble_error_t __real_patch_rom_functions(void);

static bool s_should_log_about_mic_error = false;
static uint32_t s_max_subsequent_error_count_since_last_log = 0;
static uint32_t s_subsequent_mic_errors = 0;
#define MAX_SUBSEQUENT_MIC_ERRORS (16)

bool should_log_about_mic_error(uint32_t *max_subsequent_failures) {
  if (s_should_log_about_mic_error) {
    *max_subsequent_failures = s_max_subsequent_error_count_since_last_log;
    s_should_log_about_mic_error = false;
    s_max_subsequent_error_count_since_last_log = 0;
    return true;
  }

  return false;
}

static void prv_mic_error_resilience_workaround(struct co_buf_rx_desc *rxdesc) {
  // MT: PBL-39297 -- Qualcomm WCN3660/WCN3680's encryption goes off in the weeds
  // occassionally for a couple packets...
  if (rxdesc->rxstatus & BLE_MIC_ERR_BIT) {
    if (++s_subsequent_mic_errors > s_max_subsequent_error_count_since_last_log) {
      s_max_subsequent_error_count_since_last_log = s_subsequent_mic_errors;
    }

    if (s_subsequent_mic_errors <= MAX_SUBSEQUENT_MIC_ERRORS) {
      // Pretend the MIC error was a CRC so the upper layers will ignore the packet:
      rxdesc->rxstatus &= ~BLE_MIC_ERR_BIT;
      rxdesc->rxstatus |= BLE_CRC_ERR_BIT;
    } else {
      // We are going to disconnect as a result of MIC errors, let's generate a log about this
      s_should_log_about_mic_error = true;
    }
  } else {
    if (s_subsequent_mic_errors != 0) {
      // We have recovered from the MIC error issue but let's log how many bad packets in a row we
      // received
      s_should_log_about_mic_error = true;
      s_subsequent_mic_errors = 0;
    }
  }
}

static void prv_lld_data_rx_check(struct lld_evt_tag *evt,
                                  struct lld_data_ind *msg,
                                  uint8_t rx_cnt) {
  uint8_t hdl = co_buf_rx_current_get();

  // Initialize the message
  msg->rx_cnt = rx_cnt;
  msg->rx_hdl = hdl;

  //Get the event counter
  msg->evt_cnt = evt->counter;

  // If required, copy the received buffers from exchange memory to system RAM
  while (rx_cnt--)
  {
#if (BLE_PERIPHERAL)
    struct co_buf_rx_desc *rxdesc = co_buf_rx_get(hdl);

    prv_mic_error_resilience_workaround(rxdesc);

    // If we are waiting for the acknowledgment, and it is received, enable the slave
    // latency
    if (LLD_EVT_FLAG_GET(evt, WAITING_ACK) && !(rxdesc->rxstatus & BLE_NESN_ERR_BIT))
    {
      // We received the acknowledgment
      LLD_EVT_FLAG_RESET(evt, WAITING_ACK);
    }
#endif //(BLE_PERIPHERAL)

    // Go to the next descriptor
    hdl = co_buf_rx_next(hdl);
  };

  // Move the current RX buffer
  co_buf_rx_current_set(hdl);
}

#if IS_AE_CHIP
static void prv_llm_util_get_supp_features(struct le_features *feats) {
    memcpy(&feats->feats[0], &llm_local_le_feats.feats[0], LE_FEATS_LEN);
    feats->feats[0] &= ~BLE_CON_PARAM_REQ_PROC_FEATURE;
#if !SUPPORTS_PACKET_LENGTH_EXTENSION
    feats->feats[0] &= ~BLE_LE_LENGTH_FEATURE;
#endif
}

#if !SUPPORTS_PACKET_LENGTH_EXTENSION
void llc_pdu_send_func(uint16_t conhdl, uint8_t length);

// This is called in the ROM by llcp_length_req_handler(). We want to pretend to the remote that we
// do not support the feature. To do this we progress the dialog state as if things are normal but
// when we respond to the remote we pretend the message was not understood. This should be a valid
// response per v4.2 "5.1.9 Data Length Update Procedure"
static void prv_llc_length_rsp_pdu_send(uint16_t conhdl)
{
  LLC_FLAG_RESET(conhdl, LE_LENGTH_REQ_PEND);

  // Get the TX buffer to be used
  struct co_buf_tx_desc *txdesc = co_buf_tx_desc_get(LLC_LE_CNTL_PKT_BASE_IDX + conhdl);
  struct llcp_unknown_rsp *data = (struct llcp_unknown_rsp *)co_buf_tx_buffer_get(txdesc);

  // Build the Unknown Response PDU
  data->opcode = LL_UNKNOWN_RSP;
  data->unk_type =  LL_LENGTH_REQ;

  // Send the LLCP PDU
  llc_pdu_send_func(conhdl, LL_UNKN_RSP_LEN);
}
#endif

#endif

static const uint32_t s_orig_functions[] = {
#if IS_AE_CHIP
  (uint32_t) llm_util_get_supp_features,
#if !SUPPORTS_PACKET_LENGTH_EXTENSION
  (uint32_t) llc_length_rsp_pdu_send,
#endif
#endif
  (uint32_t) lld_data_rx_check,
};

static const uint32_t s_patched_functions[] = {
#if IS_AE_CHIP
  (uint32_t) prv_llm_util_get_supp_features,
#if !SUPPORTS_PACKET_LENGTH_EXTENSION
  (uint32_t) prv_llc_length_rsp_pdu_send,
#endif
#endif
  (uint32_t) prv_lld_data_rx_check,
};

_Static_assert(ARRAY_LENGTH(s_orig_functions) == ARRAY_LENGTH(s_patched_functions), "");

void __wrap_patch_rom_functions(void) {
  // Disable all patches:
  PATCH->PATCH_VALID_RESET_REG = ~0;

  // Execute original first:
  __real_patch_rom_functions();

  const size_t max_patches = 28;
  const size_t bits_in_reg = sizeof(uint32_t) * 8;
  uint32_t patch_enabled_bits = PATCH->PATCH_VALID_REG;
  // Use count leading zeroes, assuming the LSB slots are used first.
  size_t slots_left = __builtin_clz(patch_enabled_bits) - (bits_in_reg - max_patches);

  size_t num_patches = ARRAY_LENGTH(s_patched_functions);
  PBL_ASSERTN(slots_left >= num_patches);

  // Write PATCH registers:
  int idx = max_patches - slots_left;
  uint32_t *addr_reg = ((uint32_t *)&PATCH->PATCH_ADDR0_REG) + (2 * idx);
  const uint32_t *patch_function = s_orig_functions;
  while (num_patches) {
    *addr_reg = *patch_function;
    addr_reg += 2;
    patch_enabled_bits |= (1 << idx);
    ++patch_function;
    --num_patches;
    idx++;
  }

  // Write vector table:
  idx = max_patches - slots_left;
  uint32_t *vec = ((uint32_t *)0x7fc00c0) + idx;
  memcpy(vec, s_patched_functions, sizeof(s_patched_functions));

  PATCH->PATCH_VALID_SET_REG = patch_enabled_bits;
}
