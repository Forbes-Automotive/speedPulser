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

#if enableDebug
// ===== Serial Diagnostics Task (1 Hz telemetry block) =====
// Prints one neat [SYS] line per second summarising the live running state.
// The rich per-loop [FB] PID block is emitted from applyFeedbackTrim() while
// feedback is enabled; here we only note when feedback is switched OFF so the
// [FB] tag always reflects its status.
static void diagTask(void *parameter) {
  const uint16_t kMaxDuty = PWM_DUTY_MAX;  // appliedDutyCycle is raw 12-bit hardware duty
  for (;;) {
    DEBUG("up=%lus mode=%s dir=%s in=%lu Hz speed=%u kph duty=%u/%u fb=%s",
          (unsigned long)(millis() / 1000),
          testCal ? "CAL" : (testSpeedo ? "TEST" : "RUN"),
          reverseDirection ? "REV" : "FWD",
          (unsigned long)dutyCycleIncoming,
          dutyCycle, appliedDutyCycle, (unsigned)kMaxDuty,
          feedbackEnable ? "ON" : "OFF");

    if (!feedbackEnable) {
      DEBUG_FB("disabled — enable feedback in the UI to trim measured speed to target");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
#endif  // enableDebug

// ===== Task Initialisation =====
// Create all FreeRTOS tasks
void taskInit(void) {
  DEBUG("initialising FreeRTOS tasks...");

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

#if enableDebug
  // Create 1 Hz serial diagnostics task (only when serial debug is compiled in)
  xTaskCreatePinnedToCore(
    diagTask,                      // Function to implement the task
    "diagTask",                    // Name of the task
    3072,                          // Stack size
    NULL,                          // Parameter passed
    tskIDLE_PRIORITY + 1,          // Low priority (diagnostics only)
    NULL,                          // Task handle (not needed)
    0                              // Core 0
  );
#endif

  DEBUG("all tasks created");
}
