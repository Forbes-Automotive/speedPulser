#include "speedPulser_defs.h"

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
