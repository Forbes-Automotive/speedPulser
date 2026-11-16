#include "speedPulser_resetLog.h"
#include "speedPulser_defs.h"
#include <esp_system.h>

namespace
{
constexpr uint8_t RL_LAYOUT = 1;         // bump if Store changes: an old blob is then discarded
constexpr uint8_t RL_RING = 10;          // starts kept in the history
constexpr uint32_t RL_STABLE_MS = 10000; // a start that runs this long "ran OK"

// Causes are counted in these groups (the raw esp_reset_reason_t is kept per entry).
enum : uint8_t
{
  K_POWERON,
  K_BROWNOUT,
  K_CRASH,
  K_WDT,
  K_SW,
  K_OTHER,
  K_COUNT
};

struct Entry
{
  uint32_t boot;  // boot number since the log was cleared
  uint8_t reason; // raw esp_reset_reason_t
  uint8_t ranOk;  // 1 once this start reached RL_STABLE_MS
  uint16_t pad;
};

struct Store
{
  uint8_t layout;
  uint8_t head; // next ring slot to write
  uint8_t used; // valid ring entries
  uint8_t pad;
  uint32_t boots;
  uint16_t counts[K_COUNT];
  uint16_t shortRuns; // starts that never reached RL_STABLE_MS
  Entry ring[RL_RING];
};

Store g_log;
Preferences g_pref; // own object + namespace: independent of the settings in speedPulser_eep.cpp
SemaphoreHandle_t g_mtx = nullptr;
bool g_ready = false;
bool g_ranOkMarked = false;
uint8_t g_reason = ESP_RST_UNKNOWN;

uint8_t kindOf(uint8_t r)
{
  switch ((esp_reset_reason_t)r)
  {
  case ESP_RST_POWERON:
    return K_POWERON;
  case ESP_RST_BROWNOUT:
  case ESP_RST_PWR_GLITCH:
    return K_BROWNOUT;
  case ESP_RST_PANIC:
  case ESP_RST_CPU_LOCKUP:
    return K_CRASH;
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return K_WDT;
  case ESP_RST_SW:
    return K_SW;
  default:
    return K_OTHER;
  }
}

const char *kindKey(uint8_t k)
{
  static const char *const keys[K_COUNT] = {"powerOn", "brownout", "crash", "watchdog", "software", "other"};
  return k < K_COUNT ? keys[k] : "other";
}

const char *reasonName(uint8_t r)
{
  switch ((esp_reset_reason_t)r)
  {
  case ESP_RST_POWERON:    return "Power-on";
  case ESP_RST_BROWNOUT:   return "Brown-out";
  case ESP_RST_PWR_GLITCH: return "Power glitch";
  case ESP_RST_PANIC:      return "Crash";
  case ESP_RST_CPU_LOCKUP: return "CPU lock-up";
  case ESP_RST_INT_WDT:    return "Watchdog (interrupt)";
  case ESP_RST_TASK_WDT:   return "Watchdog (task)";
  case ESP_RST_WDT:        return "Watchdog";
  case ESP_RST_SW:         return "Software restart";
  case ESP_RST_EXT:        return "Reset pin";
  case ESP_RST_DEEPSLEEP:  return "Wake from sleep";
  case ESP_RST_USB:        return "USB reset";
  case ESP_RST_JTAG:       return "JTAG reset";
  case ESP_RST_SDIO:       return "SDIO reset";
  case ESP_RST_EFUSE:      return "eFuse error";
  default:                 return "Unknown";
  }
}

const char *reasonDetail(uint8_t r)
{
  switch (kindOf(r))
  {
  case K_POWERON:
    return "Normal switch-on, or the supply dropped out completely and came back.";
  case K_BROWNOUT:
    return "The supply dipped low enough to reset the ESP32 (e.g. while cranking).";
  case K_CRASH:
    return "Firmware crash.";
  case K_WDT:
    return "Firmware stopped responding and the watchdog reset it.";
  case K_SW:
    return "Restarted by the firmware (e.g. after an OTA update).";
  default:
    return "Reset by USB / JTAG, the reset pin or another source.";
  }
}

void save()
{
  g_pref.putBytes("log", &g_log, sizeof(g_log));
}

// Append the current boot as the newest entry (caller holds the mutex or is in init).
void addCurrentBoot()
{
  g_log.boots++;
  const uint8_t k = kindOf(g_reason);
  if (g_log.counts[k] < 0xFFFF)
    g_log.counts[k]++;
  Entry &e = g_log.ring[g_log.head];
  e.boot = g_log.boots;
  e.reason = g_reason;
  e.ranOk = g_ranOkMarked ? 1 : 0;
  e.pad = 0;
  g_log.head = (g_log.head + 1) % RL_RING;
  if (g_log.used < RL_RING)
    g_log.used++;
}

void resetStore()
{
  memset(&g_log, 0, sizeof(g_log));
  g_log.layout = RL_LAYOUT;
}
} // namespace

void resetLogInit()
{
  g_mtx = xSemaphoreCreateMutex();
  g_reason = (uint8_t)esp_reset_reason();

  g_pref.begin("resetLog", false);
  bool ok = g_pref.isKey("log") &&
            g_pref.getBytesLength("log") == sizeof(g_log) &&
            g_pref.getBytes("log", &g_log, sizeof(g_log)) == sizeof(g_log) &&
            g_log.layout == RL_LAYOUT && g_log.head < RL_RING && g_log.used <= RL_RING;
  if (!ok)
    resetStore();

  // The previous start never reached RL_STABLE_MS -> it lost power / reset early.
  if (g_log.used)
  {
    const Entry &prev = g_log.ring[(g_log.head + RL_RING - 1) % RL_RING];
    if (!prev.ranOk && g_log.shortRuns < 0xFFFF)
      g_log.shortRuns++;
  }

  addCurrentBoot();
  save();
  g_ready = true;

  DEBUG("start #%lu: %s (%u) — %u short run(s) logged",
        (unsigned long)g_log.boots, reasonName(g_reason), (unsigned)g_reason, (unsigned)g_log.shortRuns);
}

void resetLogTick()
{
  if (!g_ready || g_ranOkMarked || millis() < RL_STABLE_MS)
    return;
  xSemaphoreTake(g_mtx, portMAX_DELAY);
  g_log.ring[(g_log.head + RL_RING - 1) % RL_RING].ranOk = 1;
  save();
  g_ranOkMarked = true;
  xSemaphoreGive(g_mtx);
}

void resetLogClear()
{
  if (!g_ready)
    return;
  xSemaphoreTake(g_mtx, portMAX_DELAY);
  resetStore();
  addCurrentBoot(); // the log restarts with this boot (keeps its "ran OK" state)
  save();
  xSemaphoreGive(g_mtx);
  DEBUG("reset log cleared");
}

void resetLogToJson(JsonDocument &doc)
{
  doc["uptime"] = millis() / 1000;
  doc["stableSecs"] = RL_STABLE_MS / 1000;
  doc["reason"] = reasonName(g_reason);
  doc["reasonKind"] = kindKey(kindOf(g_reason));
  doc["reasonDetail"] = reasonDetail(g_reason);
  if (!g_ready)
    return;

  xSemaphoreTake(g_mtx, portMAX_DELAY);
  Store s = g_log; // copy out, then build the JSON without holding the lock
  xSemaphoreGive(g_mtx);

  doc["boots"] = s.boots;
  doc["shortRuns"] = s.shortRuns;
  JsonObject c = doc["counts"].to<JsonObject>();
  for (uint8_t k = 0; k < K_COUNT; k++)
    c[kindKey(k)] = s.counts[k];

  JsonArray h = doc["history"].to<JsonArray>(); // newest first; [0] is this start
  for (uint8_t i = 0; i < s.used; i++)
  {
    const Entry &e = s.ring[(s.head + RL_RING - 1 - i) % RL_RING];
    JsonObject o = h.add<JsonObject>();
    o["boot"] = e.boot;
    o["reason"] = reasonName(e.reason);
    o["kind"] = kindKey(kindOf(e.reason));
    o["ranOk"] = e.ranOk != 0;
    o["current"] = (i == 0);
  }
}
