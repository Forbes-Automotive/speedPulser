#include "speedPulser_defs.h"
#include "power_manager.h"

/**
 * Connect to WiFi in Access Point mode
 */
void connectWifi()
{
  WiFi.hostname(wifiHostName);

#if serialDebugWifi
  DEBUG_PRINTLN("Starting WiFi...");
  DEBUG_PRINTLN("Creating access point...");
#endif

  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(wifiHostName);
  WiFi.setSleep(false);               // Disable sleep for UI responsiveness
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Reduce TX power for stability on C3

#if serialDebugWifi
  DEBUG_PRINT("WiFi SSID: ");
  DEBUG_PRINTLN(wifiHostName);
  DEBUG_PRINTLN("IP Address: 192.168.1.1");
#endif
}

/**
 * Disconnect WiFi if no devices connected
 * Called periodically by wifiTask
 * NOTE: kept for reference — WiFi management is now handled by power_manager.
 */
void disconnectWifi()
{
#if serialDebugWifi
  DEBUG_PRINTF("Number of connections: %d\n", WiFi.softAPgetStationNum());
#endif

  if (WiFi.softAPgetStationNum() == 0)
  {
#if serialDebugWifi
    DEBUG_PRINTLN("No connections, turning off WiFi");
#endif

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
  return WiFi.softAPgetStationNum() > 0;
}

// ACTIVE -> REDUCED: close the web server cleanly before the radio drops.
void powerOnEnterReduced()
{
  server.end();
}

// REDUCED -> ACTIVE: bring the AP and web server back. Routes are already
// registered (no need to re-run setupWebServer()), so we only restart the
// radio and the listener.
void powerOnExitReduced()
{
  connectWifi();
  server.begin();
}
