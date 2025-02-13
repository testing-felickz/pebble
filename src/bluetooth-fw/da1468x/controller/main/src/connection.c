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

#include "connection.h"
#include "connection_private.h"

#include "ble_task.h"
#include "util/list.h"
#include "kernel/pbl_malloc.h"
#include "os/mutex.h"
#include "system/logging.h"
#include "system/passert.h"

#include <bluetooth/bluetooth_types.h>
#include <btutil/bt_device.h>
#include <util/list.h>

#include <stdint.h>
#include <string.h>

extern void ppogatt_destroy_state(PPoGATTWorkAroundState *state);

typedef struct ForEachCbData {
  ConnectionForEachCallback cb;
  void *data;
} ForEachCbData;

static Connection *s_connections;

// We expect several threads to call this module (i.e ble_task, host_transport) so
// make it threadsafe
static PebbleRecursiveMutex *s_connection_lock = NULL;

void connection_module_init(void) {
  s_connection_lock = mutex_create_recursive();
}

static void prv_lock(void) {
  mutex_lock_recursive(s_connection_lock);
}

static void prv_unlock(void) {
  mutex_unlock_recursive(s_connection_lock);
}

Connection *connection_create(uint16_t conn_idx, const BTDeviceInternal *initial_addr,
                              const BTDeviceAddress *local_addr,
                              const BleConnectionParams *params) {
  ble_task_assert_is_executing_on_ble_task();
  Connection *connection;
  prv_lock();
  {
    connection = kernel_malloc_check(sizeof(Connection));
    *connection = (Connection) {
      .conn_idx = conn_idx,
      .initial_addr = *initial_addr,
      .local_addr = *local_addr,
      .conn_params = *params,
    };
    s_connections = (Connection *)list_prepend((ListNode *)s_connections, (ListNode *)connection);
  }
  prv_unlock();
  return connection;
}

void connection_destroy(Connection *connection) {
  ble_task_assert_is_executing_on_ble_task();
  prv_lock();
  {
    if (connection_is_valid(connection)) {
      list_remove((ListNode *)connection, (ListNode **)&s_connections, NULL);

      ListNode *tmp = (ListNode *) connection->gatt_op_list;
      while (tmp) {
        ListNode *next = list_get_next((ListNode *)tmp);
        kernel_free(tmp);
        tmp = next;
      }

      if (connection->ppogatt_wa_state) {
        ppogatt_destroy_state(connection->ppogatt_wa_state);
      }

      kernel_free(connection);
    }
  }
  prv_unlock();
}

bool connection_is_valid(Connection *connection) {
  bool rv;
  prv_lock();
  {
    rv = list_contains((ListNode *)s_connections, (const ListNode *)connection);
  }
  prv_unlock();
  return rv;
}

static bool prv_connection_by_idx_list_filter_cb(ListNode *found_node, void *data) {
  uint16_t conn_idx = (uintptr_t)data;
  return (((Connection *)found_node)->conn_idx == conn_idx);
}

Connection *connection_by_idx(uint16_t conn_idx) {
  Connection *connection;
  prv_lock();
  {
    connection = (Connection *)list_find(
        (ListNode *)s_connections, prv_connection_by_idx_list_filter_cb,
        (void *)(uintptr_t)conn_idx);
  }
  prv_unlock();
  return connection;
}

Connection *connection_by_idx_check(uint16_t conn_idx) {
  Connection *connection;
  prv_lock();
  {
    connection = connection_by_idx(conn_idx);
    PBL_ASSERTN(connection);
  }
  prv_unlock();
  return connection;
}

static bool prv_connection_by_address_list_filter_cb(ListNode *found_node, void *data) {
  const BTDeviceInternal *desired_addr = (const BTDeviceInternal *)data;
  const Connection *connection = (const Connection *)found_node;
  return (bt_device_internal_equal(desired_addr, &connection->initial_addr) ||
          (connection->has_updated_addr &&
           bt_device_internal_equal(desired_addr, &connection->updated_addr)));
}

Connection *connection_by_address(const BTDeviceInternal *addr) {
  Connection *connection;
  prv_lock();
  {
    connection = (Connection *)list_find(
        (ListNode *)s_connections, prv_connection_by_address_list_filter_cb, (void *)addr);
  }
  prv_unlock();
  return connection;
}

Connection *connection_by_address_check(const BTDeviceInternal *addr) {
  Connection *connection;
  prv_lock();
  {
    connection = connection_by_address(addr);
    PBL_ASSERTN(connection);
  }
  prv_unlock();
  return connection;
}

static bool prv_connection_for_each_cb(ListNode *found_node, void *data) {
  ForEachCbData *cb_data = (ForEachCbData *)data;
  cb_data->cb((Connection *)found_node, cb_data->data);
  return false; // Force not found -- iterate the entire list
}

void connection_for_each(ConnectionForEachCallback cb, void *data) {
  prv_lock();
  ForEachCbData cb_data = { .cb = cb, .data = data };
  {
    list_find((ListNode *)s_connections, prv_connection_for_each_cb, &cb_data);
  }
  prv_unlock();
}

//
// Getters
//
// Note: Using a lock on the getters doesn't really accomplish anything since
// the fields we read are either set on creation or atomic operations. We do
// run the risk that the connection handle is no longer valid and could add
// some extra checks in the future to protect against that I suppose

uint16_t connection_get_idx(Connection *connection) {
  uint16_t idx;
  prv_lock();
  {
    idx = connection->conn_idx;
  }
  prv_unlock();
  return idx;
}

void connection_get_address(const Connection *connection, BTDeviceInternal *addr_buf) {
  prv_lock();
  {
    if (connection->has_updated_addr) {
      *addr_buf = connection->updated_addr;
    } else {
      *addr_buf = connection->initial_addr;
    }
  }
  prv_unlock();
}

void connection_get_local_address(Connection *connection, BTDeviceAddress *addr_buf) {
  prv_lock();
  {
    *addr_buf = connection->local_addr;
  }
  prv_unlock();
}

void connection_get_conn_params(const Connection *connection,
                                BleConnectionParams *params_out) {
  prv_lock();
  {
    *params_out = connection->conn_params;
  }
  prv_unlock();
}

void connection_get_address_by_idx_check(uint16_t conn_idx, BTDeviceInternal *addr_out) {
  prv_lock();
  {
    Connection *connection = connection_by_idx_check(conn_idx);
    connection_get_address(connection, addr_out);
  }
  prv_unlock();
}

uint8_t connection_get_last_pairing_result(uint16_t conn_idx) {
  uint8_t rv = 0;
  prv_lock();
  {
    Connection *connection = connection_by_idx(conn_idx);
    if (connection) {
      rv = connection->last_pairing_result;
    }
  }
  prv_unlock();
  return rv;
}

PPoGATTWorkAroundState *connection_get_ppogatt_wa_state(Connection *connection) {
  PPoGATTWorkAroundState *state;
  prv_lock();
  {
    state = connection->ppogatt_wa_state;
  }
  prv_unlock();
  return state;
}

bool connection_is_gateway(Connection *connection) {
  bool val;
  prv_lock();
  {
    val = connection->is_gateway;
  }
  prv_unlock();
  return val;
}

static bool prv_get_flag(const Connection *connection, ConnectionFlag flag) {
  bool val;
  prv_lock();
  {
    uint32_t bit = (1 << flag);
    val = ((connection->flags & bit) == bit);
  }
  prv_unlock();
  return val;
}

bool connection_is_subscribed_to_connection_status_notifications(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_IsSubscribedToConnectionStatusNotifications);
}

bool connection_should_pin_address(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_ShouldPinAddress);
}

bool connection_should_auto_accept_re_pairing(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_ShouldAutoAcceptRePairing);
}

bool connection_is_reversed_ppogatt_enabled(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_IsReversedPPoGATTEnabled);
}

bool connection_is_subscribed_to_gatt_mtu_notifications(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_IsSubscribedToGattMtuNotifications);
}

bool connection_is_subscribed_to_conn_param_notifications(const Connection *connection) {
  return prv_get_flag(connection, ConnectionFlag_IsSubscribedToConnParamNotifications);
}

//
// Setters
//

void connection_set_gateway(Connection *connection, bool is_gateway) {
  prv_lock();
  {
    if (connection_is_valid(connection)) {
      connection->is_gateway = is_gateway;
    }
  }
  prv_unlock();
}

static char prv_debug_char_for_flag(ConnectionFlag flag) {
  static const char s_debug_chars[] = {
    [ConnectionFlag_IsSubscribedToConnectionStatusNotifications] = 'S',
    [ConnectionFlag_IsSubscribedToGattMtuNotifications] = 'M',
    [ConnectionFlag_IsSubscribedToConnParamNotifications] = 'C',
    [ConnectionFlag_ShouldPinAddress] = 'P',
    [ConnectionFlag_ShouldAutoAcceptRePairing] = 'A',
    [ConnectionFlag_IsReversedPPoGATTEnabled] = 'R',
  };
  return s_debug_chars[flag];
}

static void prv_set_flag(Connection *connection, bool enabled, ConnectionFlag flag) {
  prv_lock();
  {
    if (connection_is_valid(connection)) {
      PBL_LOG(LOG_LEVEL_DEBUG, "Changing connection flag: %c=%u",
              prv_debug_char_for_flag(flag), enabled);
      if (enabled) {
        connection->flags |= (1 << flag);
      } else {
        connection->flags &= ~(1 << flag);
      }
    }
  }
  prv_unlock();
}

void connection_set_subscribed_to_connection_status_notifications(
    Connection *connection, bool is_subscribed) {
  prv_set_flag(connection, is_subscribed,
               ConnectionFlag_IsSubscribedToConnectionStatusNotifications);
}

void connection_set_subscribed_to_gatt_mtu_notifications(
    Connection *connection, bool is_subscribed) {
  prv_set_flag(connection, is_subscribed,
               ConnectionFlag_IsSubscribedToGattMtuNotifications);
}

void connection_set_subscribed_to_conn_param_notifications(
    Connection *connection, bool is_subscribed) {
  prv_set_flag(connection, is_subscribed,
               ConnectionFlag_IsSubscribedToConnParamNotifications);
}

void connection_set_should_pin_address(Connection *connection, bool should_pin_address) {
  prv_set_flag(connection, should_pin_address,
               ConnectionFlag_ShouldPinAddress);
}

void connection_set_should_auto_accept_re_pairing(Connection *connection,
                                                 bool should_auto_accept_re_pairing) {
  prv_set_flag(connection, should_auto_accept_re_pairing,
               ConnectionFlag_ShouldAutoAcceptRePairing);
}

void connection_set_reversed_ppogatt_enabled(Connection *connection,
                                             bool is_reversed_ppogatt_enabled) {
  prv_set_flag(connection, is_reversed_ppogatt_enabled,
               ConnectionFlag_IsReversedPPoGATTEnabled);
}

void connection_set_last_pairing_result(uint16_t conn_idx, uint8_t result) {
  prv_lock();
  {
    Connection *connection = connection_by_idx(conn_idx);
    if (connection) {
      connection->last_pairing_result = result;
    }
  }
  prv_unlock();
}

void connection_set_ppogatt_wa_state(Connection *connection, PPoGATTWorkAroundState *state) {
  prv_lock();
  {
    connection->ppogatt_wa_state = state;
  }
  prv_unlock();
}

void connection_set_conn_params(Connection *connection, const BleConnectionParams *params) {
  prv_lock();
  {
    if (connection_is_valid(connection)) {
      connection->conn_params = *params;
    }
  }
  prv_unlock();
}

void connection_update_address(Connection *connection, const BTDeviceInternal *updated_addr) {
  prv_lock();
  {
    if (connection_is_valid(connection)) {
      connection->updated_addr = *updated_addr;
      connection->has_updated_addr = true;
    }
  }
  prv_unlock();
}

//
// Other functions
//

void connection_enqueue_gatt_op(Connection *connection, uintptr_t context_ref,
                                GattRespDest resp_dest, GattOpType op_type) {
  prv_lock();
  {
    GattOperation *node = kernel_malloc_check(sizeof(GattOperation));
    *node = (GattOperation) {
      .object_ref = context_ref,
      .resp_dest = resp_dest,
      .op_type = op_type,
    };
    connection->gatt_op_list = (GattOperation *)
        list_get_head(list_append((ListNode *)connection->gatt_op_list, (ListNode *)node));
  }
  prv_unlock();
}

bool connection_dequeue_gatt_op(Connection *connection, uintptr_t *context_ref,
                                GattRespDest *resp_dest, GattOpType expected_op_type) {
  bool rv = true;
  prv_lock();
  {
    GattOperation *tmp = connection->gatt_op_list;
    if (!tmp) {
      rv = false;
      goto unlock;
    }

    *context_ref = tmp->object_ref;
    *resp_dest = tmp->resp_dest;

    PBL_ASSERTN(tmp->op_type == expected_op_type);

    connection->gatt_op_list = (GattOperation *)list_pop_head((ListNode *)connection->gatt_op_list);
    kernel_free(tmp);
  }
unlock:
  prv_unlock();
  return rv;
}

bool connection_pop_gatt_op(Connection *connection) {
  bool rv = true;
  prv_lock();
  {
    GattOperation *tmp_tail = (GattOperation *)list_get_tail((ListNode *)connection->gatt_op_list);
    if (!tmp_tail) {
      PBL_LOG(LOG_LEVEL_WARNING, "Gatt: Attempted to pop recent op when list empty");
      PBL_ASSERTN(0);
    }

    connection->gatt_op_list = (GattOperation *)
        list_get_head(list_pop_tail((ListNode *)connection->gatt_op_list));
    kernel_free(tmp_tail);
  }
  prv_unlock();
  return rv;
}
