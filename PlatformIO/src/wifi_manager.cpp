/*
  wifi_manager — universal ESP32 SoftAP + mDNS + web-serving module (impl)
  See wifi_manager.h for the full rationale and usage notes.
*/

#include "wifi_manager.h"

#include <ESPmDNS.h>
#include <LittleFS.h>

// Optional serial logging. Define WIFI_MGR_LOG=1 (e.g. in platformio.ini build
// flags) to enable; off by default so the module drops into any project.
#ifndef WIFI_MGR_LOG
#define WIFI_MGR_LOG 0
#endif

// ---- Module state -----------------------------------------------------------
static wifimgr_config_t g_wcfg;
static bool g_initialised = false;
static bool g_mdnsUp = false;

// ---- Helpers ----------------------------------------------------------------
static void wifiLog(const char *fmt, ...)
{
#if WIFI_MGR_LOG
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("[WIFI] %s\n", buf);
#else
  (void)fmt;
#endif
}

static bool strHasContent(const char *s) { return s != nullptr && s[0] != '\0'; }

// ---- mDNS -------------------------------------------------------------------
static void wifiStartMdns(void)
{
  if (!strHasContent(g_wcfg.mdnsName))
    return;

  if (g_mdnsUp)
  {
    MDNS.end();
    g_mdnsUp = false;
  }

  if (MDNS.begin(g_wcfg.mdnsName))
  {
    MDNS.addService("http", "tcp", 80);
    g_mdnsUp = true;
    wifiLog("mDNS up: http://%s.local", g_wcfg.mdnsName);
  }
  else
  {
    wifiLog("mDNS begin failed");
  }
}

// ---- Public API -------------------------------------------------------------
wifimgr_config_t wifiDefaultConfig(void)
{
  wifimgr_config_t c = {};
  c.hostName = "ESP32";
  c.mdnsName = nullptr;
  c.fwVersion = nullptr;
  c.apPassword = nullptr;

  c.apChannel = 1;
  c.apHidden = false;
  c.apMaxClients = 4;

  c.apIp = IPAddress(192, 168, 1, 1);
  c.apGateway = IPAddress(192, 168, 1, 1);
  c.apSubnet = IPAddress(255, 255, 255, 0);

  c.disableWifiSleep = true;
  c.mountLittleFS = true;
  c.indexPath = "/index.html";
  return c;
}

void wifiManagerStartAP(void)
{
  if (!g_initialised)
    return;

  WiFi.mode(WIFI_AP);
  if (strHasContent(g_wcfg.hostName))
    WiFi.softAPsetHostname(g_wcfg.hostName);

  WiFi.softAPConfig(g_wcfg.apIp, g_wcfg.apGateway, g_wcfg.apSubnet);

  // Open network by default; no captive-portal DNS so the phone keeps mobile
  // data and simply sees a hotspot with no internet.
  const char *pwd = strHasContent(g_wcfg.apPassword) ? g_wcfg.apPassword : nullptr;
  WiFi.softAP(g_wcfg.hostName, pwd, g_wcfg.apChannel, g_wcfg.apHidden ? 1 : 0,
              g_wcfg.apMaxClients);

  if (g_wcfg.disableWifiSleep)
    WiFi.setSleep(false);

  wifiLog("SoftAP \"%s\" @ %s", g_wcfg.hostName, WiFi.softAPIP().toString().c_str());

  wifiStartMdns();
}

void wifiManagerStopAP(void)
{
  if (g_mdnsUp)
  {
    MDNS.end();
    g_mdnsUp = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiLog("SoftAP stopped");
}

void wifiManagerInit(const wifimgr_config_t *cfg)
{
  g_wcfg = cfg ? *cfg : wifiDefaultConfig();
  g_initialised = true;

  if (g_wcfg.mountLittleFS)
  {
    if (!LittleFS.begin(true))
      wifiLog("LittleFS mount failed");
  }

  wifiManagerStartAP();
}

void wifiManagerAttachStatic(AsyncWebServer &server)
{
  // index.html: served uncached and verbatim (no runtime substitution). The
  // firmware version is hard-coded in the HTML and updated by hand on release.
  auto serveIndex = [](AsyncWebServerRequest *request)
  {
    const char *path = strHasContent(g_wcfg.indexPath) ? g_wcfg.indexPath : "/index.html";
    if (!LittleFS.exists(path))
    {
      request->send(404, "text/plain", "index not found");
      return;
    }
    AsyncWebServerResponse *res = request->beginResponse(LittleFS, path, "text/html");
    res->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    res->addHeader("Pragma", "no-cache");
    res->addHeader("Expires", "0");
    request->send(res);
  };

  server.on("/", HTTP_GET, serveIndex);
  server.on("/index.html", HTTP_GET, serveIndex);

  // Everything else (app.js, style.css, images) is versioned via the query
  // string in index.html, so it is safe to cache aggressively.
  server.serveStatic("/", LittleFS, "/").setCacheControl("max-age=31536000");
}

const char *wifiManagerFwVersion(void)
{
  return strHasContent(g_wcfg.fwVersion) ? g_wcfg.fwVersion : "";
}

const char *wifiManagerMdnsName(void)
{
  return strHasContent(g_wcfg.mdnsName) ? g_wcfg.mdnsName : "";
}
