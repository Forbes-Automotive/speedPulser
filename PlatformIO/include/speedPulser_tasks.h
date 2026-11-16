#ifndef SPEED_PULSER_TASKS_H
#define SPEED_PULSER_TASKS_H

#include <Arduino.h>

// Two tasks: the 100 ms control task (speedPulser_control.cpp) and the settings
// save task. WiFi power is handled by power_manager (see power_manager.h).
#define SETTINGS_TASK_PRIORITY    (tskIDLE_PRIORITY + 1)
#define SPEED_TASK_PRIORITY       (tskIDLE_PRIORITY + 2) // control runs ahead of saving

#define SETTINGS_TASK_STACK_SIZE  4096
#define SPEED_TASK_STACK_SIZE     8192 // Auto Fill saves the backup table (JSON + NVS) from this task

void taskInit(void);

#endif // SPEED_PULSER_TASKS_H
