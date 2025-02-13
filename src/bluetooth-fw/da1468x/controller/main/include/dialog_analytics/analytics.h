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

#pragma once

#include "hc_protocol/hc_endpoint_analytics.h"

#include <inttypes.h>

//! Called once during initialization.
void analytics_init(void);

//! Called every time the Analytics module should reset its state and set all node values to 0.
//! Typically called after all analytics have been flushed out to the Main FW (every hour or so).
void analytics_reset_nodes(void);

//! Set a scalar metric
//! @param metric The metric to set
//! @param val The new value
void analytics_set(DialogAnalyticsMetric metric, uint32_t val);

//! Increment a metric by 1
//! @param metric The metric to increment
void analytics_inc(DialogAnalyticsMetric metric);

//! Increment a metric
//! @param metric The metric to increment
//! @param amount The amount to increment by
void analytics_add(DialogAnalyticsMetric metric, uint32_t amount);

//! Starts a stopwatch that integrates a "rate of things" over time.
//! @param metric The metric of the stopwatch to start
void analytics_stopwatch_start(DialogAnalyticsMetric metric);

//! Starts a stopwatch that integrates a "rate of things" over time.
//! @param metric The metric for which to start the stopwatch.
//! @param count_per_second The rate in number of things per second to count.
//! For example, if you want to measure "bytes transferred" over time and know the transfer speed
//! is 1024 bytes per second, then you would pass in 1024 as count_per_second.
void analytics_stopwatch_start_at_rate(DialogAnalyticsMetric metric, uint32_t count_per_second);

//! Stops a stopwatch
//! @param metric The metric of the stopwatch
void analytics_stopwatch_stop(DialogAnalyticsMetric metric);

//
// Consumer API
//

typedef void (*AnalyticsEachCallback)(DialogAnalyticsMetric metric, uint32_t value, void *context);

void analytics_each(AnalyticsEachCallback cb, void *context);
