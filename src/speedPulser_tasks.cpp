#include "speedPulser_tasks.h"
#include "speedPulser_defs.h"
#include "speedPulser_control.h"

// ===== Task Handles =====
TaskHandle_t eepromTaskHandle = NULL;
TaskHandle_t speedControlTaskHandle = NULL;

// ===== EEPROM Task =====
// Saves preferences to EEPROM every 2 seconds
void eepromTask(void *parameter) {
  const uint32_t DELAY_SPEED = eepRefresh;  // 2000ms

  while (1) {
    if (!testSpeedo) {
      writeEEP();
    }
    vTaskDelay(pdMS_TO_TICKS(DELAY_SPEED));
  }
}

// NOTE: WiFi management has been moved to power_manager (see power_manager.h).
// The old wifiTask (which disconnected WiFi after inactivity) is superseded by
// the power_manager background task, which also handles CPU frequency scaling
// and Bluetooth release. powerInit() is called from main.cpp after connectWifi()
// and setupWebServer(). The powerIsBusy / powerOnEnterReduced / powerOnExitReduced
// hooks live in speedPulser_wifi.cpp.

// ===== Task Initialisation =====
// Create all FreeRTOS tasks
void taskInit(void) {
  DEBUG_PRINTLN("Initialising FreeRTOS tasks...");

  // Create EEPROM save task
  xTaskCreatePinnedToCore(
    eepromTask,                    // Function to implement the task
    "eepromTask",                  // Name of the task
    EEPROM_TASK_STACK_SIZE,        // Stack size
    NULL,                          // Parameter passed
    EEPROM_TASK_PRIORITY,          // Task priority
    &eepromTaskHandle,             // Task handle
    0                              // Core 0
  );

  // Create speed control task
  xTaskCreatePinnedToCore(
    speedControlTask,              // Function to implement the task
    "speedControlTask",            // Name of the task
    SPEED_TASK_STACK_SIZE,         // Stack size
    NULL,                          // Parameter passed
    SPEED_TASK_PRIORITY,           // Task priority
    &speedControlTaskHandle,       // Task handle
    0                              // Core 0 (priority for speed control)
  );

  DEBUG_PRINTLN("All tasks created successfully!");
}
