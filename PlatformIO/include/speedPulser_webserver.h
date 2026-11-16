#ifndef SPEEDPULSER_WEBSERVER_H
#define SPEEDPULSER_WEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Web server + JSON API (see speedPulser_webserver.cpp for the routes)
void setupWebServer();

void handleGetSettings(AsyncWebServerRequest *request);
void handleGetStatus(AsyncWebServerRequest *request);
void handleGetCalibrations(AsyncWebServerRequest *request);
void handleGetCalCurve(AsyncWebServerRequest *request);
void handleGetBoardTest(AsyncWebServerRequest *request);
void handleGetResetLog(AsyncWebServerRequest *request);
void handleGetCal(AsyncWebServerRequest *request);
void handlePostControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handlePostAction(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handlePostCal(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif
