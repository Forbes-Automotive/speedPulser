#ifndef SPEEDPULSER_WEBSERVER_H
#define SPEEDPULSER_WEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Web server initialization and API endpoints
void setupWebServer();

// API endpoint handlers
void handleGetSettings(AsyncWebServerRequest *request);
void handleGetCalibrations(AsyncWebServerRequest *request);
void handleGetStatus(AsyncWebServerRequest *request);
void handleGetTestStatus(AsyncWebServerRequest *request);
void handleGetVersion(AsyncWebServerRequest *request);
void handlePostControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handlePostAction(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif
