#include "speedPulser_tasks.h"
#include "speedPulser_defs.h"
#include "speedPulser_control.h"

// ===== Task Handles =====
TaskHandle_t eepromTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
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

// ===== WiFi Task =====
// After an initial 60 s grace period, checks every 30 s whether any stations
// are still connected. Turns WiFi off and self-deletes once no clients remain.
void wifiTask(void *parameter) {
  const uint32_t INITIAL_DELAY  = wifiDisable;       // 60 000 ms — let user connect
  const uint32_t CHECK_INTERVAL = 30000;             // 30 s between re-checks
  bool hadClientConnection = false;

  // Wait for the grace period before the first check
  vTaskDelay(pdMS_TO_TICKS(INITIAL_DELAY));

  while (1) {
    const int stationCount = WiFi.softAPgetStationNum();

    if (stationCount > 0) {
      hadClientConnection = true;
    }

#if serialDebugWifi
    DEBUG_PRINTLN("WiFi task: checking station count...");
#endif

    if (hadClientConnection && stationCount == 0) {
      // No clients connected — shut WiFi down and exit
#if serialDebugWifi
      DEBUG_PRINTLN("No stations connected, turning off WiFi");
#endif
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
#if serialDebugWifi
      DEBUG_PRINTLN("WiFi turned off");
#endif
      vTaskDelete(NULL);
    }

#if serialDebugWifi
    if (!hadClientConnection) {
      DEBUG_PRINTLN("No clients have connected yet, keeping WiFi on");
    } else {
      DEBUG_PRINTF("Stations still connected: %d - keeping WiFi on\n", stationCount);
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL));
  }
}

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

  // Create WiFi disconnect task
  xTaskCreatePinnedToCore(
    wifiTask,                      // Function to implement the task
    "wifiTask",                    // Name of the task
    WIFI_TASK_STACK_SIZE,          // Stack size
    NULL,                          // Parameter passed
    WIFI_TASK_PRIORITY,            // Task priority
    &wifiTaskHandle,               // Task handle
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
