#include "speedPulser_tasks.h"
#include "speedPulser_defs.h"
#include "speedPulser_control.h"

// Settings save task: writes every setting every 2 s. NVS only touches flash for
// values that changed, so this costs nothing when nothing changes. It also runs
// during Speed Test (V5.06: it used to skip then, so changes made while testing
// were lost at the next power-off).
static void settingsSaveTask(void *parameter)
{
  for (;;)
  {
    writeEEP();
    vTaskDelay(pdMS_TO_TICKS(SETTINGS_SAVE_INTERVAL_MS));
  }
}

void taskInit(void)
{
  xTaskCreatePinnedToCore(settingsSaveTask, "settingsSave", SETTINGS_TASK_STACK_SIZE, NULL,
                          SETTINGS_TASK_PRIORITY, NULL, 0);
  xTaskCreatePinnedToCore(speedControlTask, "speedControl", SPEED_TASK_STACK_SIZE, NULL,
                          SPEED_TASK_PRIORITY, NULL, 0);
  DEBUG("tasks started");
}
