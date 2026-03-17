#pragma once

#include <ESPAsyncWebServer.h>
#include <MycilaESPConnect.h>
#include "ReContext.h"

class ReServer : public AsyncWebServer
{
public:
    ReServer(uint16_t port);

    void begin();
    void setESPConnect(Mycila::ESPConnect *esp);
    void pressRequestNotifyJson(const std::string& resultData);

private:
    void setAuthenticationMiddleware();
    void setHandlers();

    void handleRoot(AsyncWebServerRequest *request);
    void handleNotFound(AsyncWebServerRequest *request);
    void heapHandler(AsyncWebServerRequest *request);
    void adminClearHandler(AsyncWebServerRequest *request);
    void adminHandler(AsyncWebServerRequest *request);
    void adminSettingsGetHandler(AsyncWebServerRequest *request);
    void adminSettingsPostHandler(AsyncWebServerRequest *request, JsonVariant &json);
    void adminRestartHandler(AsyncWebServerRequest *request);
    void adminSafebootHandler(AsyncWebServerRequest *request);
    void adminDecommissionHandler(AsyncWebServerRequest *request);
    void switchbotPressHandler(AsyncWebServerRequest *request);
    void switchbotCommandHandler(AsyncWebServerRequest *request);

    ReContext ctx;
    Mycila::ESPConnect *espConnect;
    AsyncAuthenticationMiddleware basicAuth;
    AsyncWebServerRequestPtr pressRequest;
};
