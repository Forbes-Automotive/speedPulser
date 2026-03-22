#ifndef SPEED_PULSER_TASKS_H
#define SPEED_PULSER_TASKS_H

#include <Arduino.h>

// ===== Task Handles =====
extern TaskHandle_t eepromTaskHandle;
extern TaskHandle_t wifiTaskHandle;
extern TaskHandle_t speedControlTaskHandle;

// ===== Task Configuration =====
#define EEPROM_TASK_PRIORITY    (tskIDLE_PRIORITY + 1)
#define WIFI_TASK_PRIORITY      (tskIDLE_PRIORITY + 1)
#define SPEED_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)  // Higher priority for speed control

#define EEPROM_TASK_STACK_SIZE  4096
#define WIFI_TASK_STACK_SIZE    8192
#define SPEED_TASK_STACK_SIZE   2048

// ===== Task Function Declarations =====
void eepromTask(void *parameter);    // EEPROM save task (every 2 seconds)
void wifiTask(void *parameter);      // WiFi disconnect task (every 60 seconds)
void taskInit(void);                 // Initialize all tasks

#endif  // SPEED_PULSER_TASKS_H
