#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

/*
  wifi_manager — universal ESP32 SoftAP + mDNS + web-serving module
  -----------------------------------------------------------------
  Drop-in, project-agnostic WiFi front-end for the Forbes Automotive ESP32
  projects (MFSW controller, Can2Cluster, OpenHaldex, etc.) so every project
  brings its network up the same way. Companion to power_manager.

  What it standardises
  --------------------
    1. Starts a SoftAP on a fixed IP (default 192.168.1.1) with an open SSID
       named after the project.
    2. Registers an mDNS responder so the UI is reachable by name —
       http://<mdnsName>.local (e.g. mfsw.local, c2c.local, openhaldex.local)
       as well as by IP.
    3. Mounts LittleFS and serves the web UI from it.
    4. Solves the "phone caches the old UI after a firmware update" problem by
       serving index.html uncached and rewriting its asset links with the
       firmware version (see "Cache-busting" below).

  Why it appears as a normal hotspot (phone data stays available)
  ---------------------------------------------------------------
  This module deliberately does NOT run a captive-portal DNS server (the trick
  where every hostname resolves to 192.168.1.1). A captive portal makes the
  phone believe it must "sign in" and hijacks all name lookups, which kills the
  phone's ability to use mobile data while connected. By using mDNS only, the
  ESP32 looks like an ordinary WiFi hotspot with no internet: modern phones
  keep using cellular for data and still resolve <name>.local to the device.

  Cache-busting (forces the phone to re-cache after an OTA update)
  ---------------------------------------------------------------
  Browsers aggressively cache index.html / app.js / style.css from a local AP,
  so a firmware update used to require a manual cache clear. The fix here:
    - index.html is served with `Cache-Control: no-store`, so the browser
      always fetches a fresh copy from the device.
    - Any occurrence of the token %FW_VERSION% inside index.html is replaced
      with the running firmware version before it is sent. Point your asset
      links at the version:
          <link rel="stylesheet" href="style.css?v=%FW_VERSION%">
          <script src="app.js?v=%FW_VERSION%"></script>
    - Because the query string changes on every version bump, the browser sees
      a new URL and re-downloads app.js / style.css automatically. Between
      updates the assets stay cached (fast), so you get correctness AND speed
      with no manual cache clearing.

  How to use (any project)
  ------------------------
    #include "wifi_manager.h"

    // In setup(), BEFORE registering your API routes:
    wifimgr_config_t wcfg = wifiDefaultConfig();
    wcfg.hostName  = "MFSWController"; // SoftAP SSID + WiFi hostname
    wcfg.mdnsName  = "mfsw";           // -> http://mfsw.local
    wcfg.fwVersion = FW_VERSION;       // injected into index.html for cache-busting
    wifiManagerInit(&wcfg);            // brings up AP + mDNS + LittleFS

    // Register your own /api/... routes on your AsyncWebServer here, THEN:
    wifiManagerAttachStatic(server);   // static file serving + cache-busting
    server.begin();

    // power_manager integration (optional): on wake, bring the radio back with
    //   void powerOnExitReduced() { wifiManagerStartAP(); server.begin(); }
    // and drop it cleanly with
    //   void powerOnEnterReduced() { server.end(); wifiManagerStopAP(); }

  Plain C++/Arduino, depends only on WiFi, ESPmDNS, LittleFS and
  ESPAsyncWebServer (already used by these projects).
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// ---- Configuration ----------------------------------------------------------
typedef struct
{
  const char *hostName;   // SoftAP SSID + WiFi.hostname (e.g. "MFSWController")
  const char *mdnsName;   // http://<mdnsName>.local (e.g. "mfsw"). NULL/"" = skip mDNS.
  const char *fwVersion;  // firmware version string injected for cache-busting (e.g. "1.03")
  const char *apPassword; // NULL or "" = open network (recommended for in-car use)

  uint8_t apChannel;      // SoftAP channel (1-13, default 1)
  bool apHidden;          // hide the SSID (default false)
  uint8_t apMaxClients;   // max associated stations (default 4)

  IPAddress apIp;         // SoftAP IP        (default 192.168.1.1)
  IPAddress apGateway;    // SoftAP gateway   (default 192.168.1.1)
  IPAddress apSubnet;     // SoftAP subnet    (default 255.255.255.0)

  bool disableWifiSleep;  // WiFi.setSleep(false) for a snappier server.
                          // Leave true here; power_manager re-applies modem
                          // sleep on its own schedule.

  bool mountLittleFS;     // LittleFS.begin(true) at init (default true)

  const char *indexPath;  // web root index file (default "/index.html")
} wifimgr_config_t;

// Sensible defaults: open AP at 192.168.1.1, channel 1, LittleFS mounted,
// no mDNS name (set mdnsName yourself), no version string.
wifimgr_config_t wifiDefaultConfig(void);

// Store the config, mount LittleFS (if requested) and bring the SoftAP + mDNS
// up. Safe to call once from setup(). Does NOT start the web server — register
// your routes, call wifiManagerAttachStatic() then server.begin() yourself.
void wifiManagerInit(const wifimgr_config_t *cfg);

// (Re)start the SoftAP and mDNS responder using the stored config. Use this on
// wake from power_manager's REDUCED state. No-op if wifiManagerInit() was never
// called.
void wifiManagerStartAP(void);

// Stop the mDNS responder and turn the radio off (for power_manager's REDUCED
// state). Stop your web server first.
void wifiManagerStopAP(void);

// Register static-file serving on the given server with firmware cache-busting:
// index.html is served uncached with %FW_VERSION% substituted; all other files
// are served from LittleFS with a long cache lifetime (versioned via the query
// string). Call this AFTER your own API routes and BEFORE server.begin().
void wifiManagerAttachStatic(AsyncWebServer &server);

// The firmware version string passed in the config (never NULL; "" if unset).
const char *wifiManagerFwVersion(void);

// The mDNS host in use, without the .local suffix (never NULL; "" if unset).
const char *wifiManagerMdnsName(void);

#endif // WIFI_MANAGER_H
