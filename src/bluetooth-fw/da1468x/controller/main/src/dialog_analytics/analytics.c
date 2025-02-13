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

#include "dialog_analytics/analytics.h"

#include "hc_protocol/hc_endpoint_analytics.h"
#include "kernel/pbl_malloc.h"
#include "kernel/util/freertos_utils.h"
#include "system/passert.h"

#include <util/list.h>
#include <util/size.h>
#include <os/mutex.h>

// FreeRTOS
#include "FreeRTOSConfig.h"

// Dialog SDK
#include "sys_rtc.h"

#include <stdint.h>

#define MS_PER_SECOND (1000)

// Stopwatch
typedef struct {
  ListNode node;
  DialogAnalyticsMetric metric;
  uint32_t starting_ticks;
  uint32_t count_per_sec;
} AnalyticsStopwatchNode;

// Analytic Containers
typedef struct {
  ListNode node;
  DialogAnalyticsMetric metric;
  uint32_t value;
} AnalyticsNode;

static ListNode *s_analytics_list = NULL;
static ListNode *s_stopwatch_list = NULL;
static PebbleRecursiveMutex *s_analytics_lock = NULL;

// Locking Functions

static void prv_lock(void) {
  mutex_lock_recursive(s_analytics_lock);
}

static void prv_unlock(void) {
  mutex_unlock_recursive(s_analytics_lock);
}

// Analytic Node Functions

static bool prv_compare_analytic(ListNode *found_node, void *data) {
  AnalyticsNode *node = (AnalyticsNode *)found_node;
  return (node->metric == (DialogAnalyticsMetric)data);
}

static AnalyticsNode *prv_find_analytic(DialogAnalyticsMetric metric) {
  return (AnalyticsNode *)list_find(
      s_analytics_list, prv_compare_analytic, (void *)(uintptr_t)metric);
}

static void prv_add_analytic_node(DialogAnalyticsMetric metric) {
  AnalyticsNode *node = kernel_malloc_check(sizeof(AnalyticsNode));
  *node = (AnalyticsNode) {
    .metric = metric,
  };
  s_analytics_list = list_prepend(s_analytics_list, (ListNode *)node);
}

// Stopwatch Node Functions

static bool prv_compare_stopwatch(ListNode *found_node, void *data) {
  AnalyticsStopwatchNode *stopwatch = (AnalyticsStopwatchNode *)found_node;
  return (stopwatch->metric == (DialogAnalyticsMetric)data);
}

static AnalyticsStopwatchNode *prv_find_stopwatch_node(DialogAnalyticsMetric metric) {
  return (AnalyticsStopwatchNode *)list_find(
    s_stopwatch_list, prv_compare_stopwatch, (void *)(uintptr_t)metric);
}

static void prv_add_stopwatch(DialogAnalyticsMetric metric, uint32_t count_per_sec) {
  AnalyticsStopwatchNode *stopwatch = kernel_malloc_check(sizeof(AnalyticsStopwatchNode));
  *stopwatch = (AnalyticsStopwatchNode) {
    .metric = metric,
    .count_per_sec = count_per_sec,
    .starting_ticks = rtc_get(),
  };
  s_stopwatch_list = list_prepend(s_stopwatch_list, (ListNode *)stopwatch);
}

static uint32_t prv_rtc_ticks_to_ms(uint32_t rtc_ticks) {
  return (uint32_t)(((uint32_t)rtc_ticks) * MS_PER_SECOND / configSYSTICK_CLOCK_HZ);
}

static uint32_t prv_stopwatch_elapsed_ms(AnalyticsStopwatchNode *stopwatch,
                                         uint32_t current_ticks) {
  // Even if current_ticks is less than stopwatch->starting_ticks (because `rtc_get` rv rolled
  // over), this operation will still work correctly since we only use uint32_t
  // See unit test `test_dialog_analytics__stopwatch_rtc_rollover` for validation.
  const uint32_t difference = current_ticks - stopwatch->starting_ticks;
  const uint32_t dt_ms = prv_rtc_ticks_to_ms(difference);
  return (((uint32_t) stopwatch->count_per_sec) * dt_ms) / MS_PER_SECOND;
}

// API Functions

// Used primarily for unit tests
void analytics_init_private(uint32_t num_nodes) {
  s_analytics_lock = mutex_create_recursive();

  // create and populate list of all Analytics
  for (uint32_t i = 0; i < num_nodes; i++) {
    prv_add_analytic_node(i);
  }
}

void analytics_init(void) {
  analytics_init_private(DialogAnalyticMetric_Count);
}

void analytics_reset_nodes(void) {
  prv_lock();

  // Zero out all values
  ListNode *iter = s_analytics_list;
  while (iter) {
    AnalyticsNode *node = (AnalyticsNode *)iter;
    node->value = 0;
    iter = iter->next;
  }

  prv_unlock();
}

void analytics_set(DialogAnalyticsMetric metric, uint32_t value) {
  prv_lock();

  AnalyticsNode *node = prv_find_analytic(metric);
  if (node) {
    node->value = value;
  }

  prv_unlock();
}

void analytics_add(DialogAnalyticsMetric metric, uint32_t amount) {
  prv_lock();

  AnalyticsNode *node = prv_find_analytic(metric);
  if (node) {
    node->value += amount;
  }

  prv_unlock();
}

void analytics_inc(DialogAnalyticsMetric metric) {
  analytics_add(metric, 1);
}

void analytics_stopwatch_start_at_rate(DialogAnalyticsMetric metric, uint32_t count_per_second) {
  prv_lock();
  prv_add_stopwatch(metric, count_per_second);
  prv_unlock();
}

void analytics_stopwatch_start(DialogAnalyticsMetric metric) {
  analytics_stopwatch_start_at_rate(metric, MS_PER_SECOND);
}

void analytics_stopwatch_stop(DialogAnalyticsMetric metric) {
  prv_lock();
  AnalyticsStopwatchNode *stopwatch = prv_find_stopwatch_node(metric);
  if (stopwatch) {
    analytics_add(metric, prv_stopwatch_elapsed_ms(stopwatch, rtc_get()));
    list_remove(&stopwatch->node, &s_stopwatch_list, NULL);
  }
  prv_unlock();
}

void analytics_stopwatches_update(uint32_t current_ticks) {
  prv_lock();

  ListNode *cur = s_stopwatch_list;
  while (cur != NULL) {
    AnalyticsStopwatchNode *node = (AnalyticsStopwatchNode *)cur;
    analytics_add(node->metric, prv_stopwatch_elapsed_ms(node, current_ticks));
    node->starting_ticks = current_ticks;
    cur = cur->next;
  }

  prv_unlock();
}

// Getters

void analytics_each(AnalyticsEachCallback cb, void *context) {
  analytics_stopwatches_update(rtc_get());

  prv_lock();
  ListNode *iter = s_analytics_list;
  while (iter) {
    AnalyticsNode *node = (AnalyticsNode *)iter;
    cb(node->metric, node->value, context);
    iter = iter->next;
  }
  prv_unlock();
}


// Unit Test Helpers

uint32_t analytics_get_value(DialogAnalyticsMetric metric) {
  AnalyticsNode *node = prv_find_analytic(metric);
  if (node) {
    return node->value;
  }
  return 0;
}

void analytics_deinit(void) {
  while (s_stopwatch_list) {
    ListNode *head = s_stopwatch_list;
    list_remove(head, &s_stopwatch_list, NULL);
    kernel_free(head);
  }
  s_stopwatch_list = NULL;

  while (s_analytics_list) {
    ListNode *head = s_analytics_list;
    list_remove(head, &s_analytics_list, NULL);
    kernel_free(head);
  }
  s_analytics_list = NULL;

  mutex_destroy((PebbleMutex *)s_analytics_lock);
}
