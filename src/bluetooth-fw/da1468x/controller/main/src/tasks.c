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

#include "tasks.h"
#include "system/logging.h"

#include "mcu/interrupts.h"

OS_TASK DialogTaskList[DialogTask_Last] = { 0, };

DialogTask task_to_dialogtask(OS_TASK task) {
  for (int index = 0; index < DialogTask_Last; ++index) {
    if (task == DialogTaskList[index]) {
      return index;
    }
  }
  return DialogTask_Error;
}

void tasks_dump_free_space(void) {
  for (int i = 0; i < DialogTask_Last; i++) {
    OS_TASK task = DialogTaskList[i];
    if (task != 0) {
      PBL_LOG(LOG_LEVEL_DEBUG, "Task %s stack has %d bytes free", pcTaskGetTaskName(task),
              ((int)uxTaskGetStackHighWaterMark(task) * sizeof(StackType_t)));
    }
  }
}

DialogTask task_get_dialogtask(void) {
  if (mcu_state_is_isr()) {
    return DialogTask_ISR;
  }
  return task_to_dialogtask(xTaskGetCurrentTaskHandle());
}

void task_unregister_task(OS_TASK task) {
  DialogTask index = task_to_dialogtask(task);
  if (index >= DialogTask_Last) {
    return;
  }
  DialogTaskList[index] = NULL;
}

void task_unregister_dialogtask(DialogTask task) {
  if (task >= DialogTask_Last) {
    return;
  }
  DialogTaskList[task] = NULL;
}
