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

#include "osal.h"

/*
 * Centralized Task Management
 */

typedef enum {
  DialogTask_SysInit = 0,
  DialogTask_Ble,
  DialogTask_HostTrans,
  DialogTask_Logging,
  DialogTask_Last,
  DialogTask_Error = DialogTask_Last,
  DialogTask_ISR = 0xF,
} DialogTask;

extern OS_TASK DialogTaskList[DialogTask_Last];

/*
 * Task Register
 * Directly write to DialogTaskList[] for easy compatibility with OS_TASK_CREATE.
 */

/*
 * Task Unregister
 */
void task_unregister_task(OS_TASK task);
void task_unregister_dialogtask(DialogTask task);

/* Given FreeRTOS task handle, return DialogTask enum. DialogTask_Error if not found. */
DialogTask task_to_dialogtask(OS_TASK task);

/* For logging: get the current task ID (will respond with DialogTask_ISR if appropriate) */
DialogTask task_get_dialogtask(void);

/* Dumps to the console, the amount of untouched stack space for each registered task */
void tasks_dump_free_space(void);
