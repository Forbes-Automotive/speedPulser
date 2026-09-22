#include "speedPulser_defs.h"
#include "speedPulser_ver.h"
#include "power_manager.h"
#include "wifi_manager.h"
#include "ota_manager.h"

/**
 * Connect to WiFi in Access Point mode
 */
void connectWifi()
{
  DEBUG_WIFI("starting soft-AP...");

  // Common SoftAP + mDNS + LittleFS front-end (reachable at speedpulser.local).
  wifimgr_config_t wcfg = wifiDefaultConfig();
  wcfg.hostName  = wifiHostName;    // SoftAP SSID + hostname
  wcfg.mdnsName  = "speedpulser";   // -> http://speedpulser.local
  wcfg.fwVersion = FW_VERSION;      // substituted for %FW_VERSION% in index.html
  // MUST precede wifiManagerInit(): that mounts the web-UI filesystem via
  // otaFsMountSafe(), so ota_manager has to be configured first or a failed
  // mount passes silently.
  ota_config_t ocfg = otaDefaultConfig();
  ocfg.fwVersion  = FW_VERSION;
  ocfg.product    = "SpeedPulser";
  ocfg.githubRepo = "Forbes-Automotive/speedPulser"; // Releases/ + releases.json for "Check for updates"
  otaManagerInit(&ocfg);

  wifiManagerInit(&wcfg);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // reduce TX power for stability on C3

  DEBUG_WIFI("soft-AP up — SSID=%s  IP=192.168.1.1", wifiHostName);
}

/**
 * Disconnect WiFi if no devices connected
 * Called periodically by wifiTask
 * NOTE: kept for reference — WiFi management is now handled by power_manager.
 */
void disconnectWifi()
{
  DEBUG_WIFI("clients connected: %d", WiFi.softAPgetStationNum());

  if (WiFi.softAPgetStationNum() == 0)
  {
    DEBUG_WIFI("no clients — turning WiFi off");

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
}

// ----------------------------------------------------------------------------
// power_manager integration (universal reduced-power module)
// ----------------------------------------------------------------------------
// These override the weak hooks in power_manager.cpp. The device stays fully
// awake while ANY client is associated to the AP. Once the last client leaves,
// the manager's idle timer runs, then turns the radio off and drops the CPU
// clock. A power-cycle (ignition off/on) brings WiFi back automatically.
// On LOLIN C3 Mini, powerDefaultConfig() automatically caps active CPU at
// 160 MHz and skips the onboard LED (WS2812B, not a plain GPIO).

bool powerIsBusy()
{
  // ... or a browser has hit us in the last 30 s (a phone on the home router
  // in bridge mode is not an AP station).
  return WiFi.softAPgetStationNum() > 0 || otaInProgress() || otaWebClientActive();
}

// ACTIVE -> REDUCED: close the web server cleanly before the radio drops.
void powerOnEnterReduced()
{
  DEBUG_WIFI("entering reduced power — no clients, stopping web server + radio");
  server.end();
  wifiManagerStopAP();
}

// REDUCED -> ACTIVE: bring the AP and web server back. Routes are already
// registered (no need to re-run setupWebServer()), so we only restart the
// radio and the listener.
void powerOnExitReduced()
{
  DEBUG_WIFI("exiting reduced power — restarting soft-AP + web server");
  wifiManagerStartAP();
  server.begin();
}
