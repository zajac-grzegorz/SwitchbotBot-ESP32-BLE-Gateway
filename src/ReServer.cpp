#include "ReServer.h"
#include "ReContext.h"
#include "ReCommon.h"
#include <MycilaSystem.h>
#include <Matter.h>

ReServer::ReServer(uint16_t port) : AsyncWebServer(port)
{
}

void ReServer::setESPConnect(Mycila::ESPConnect *esp)
{
    espConnect = esp;
}

void ReServer::pressRequestNotifyJson(const std::string &resultData)
{
    if (auto request = pressRequest.lock())
    {
        JsonDocument doc;
        doc["status"] = resultData.substr(0, 2);
        doc["payload"] = resultData.substr(2);

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    }
}

void ReServer::begin()
{
    setAuthenticationMiddleware();
    setHandlers();
    AsyncWebServer::begin();
}

void ReServer::setAuthenticationMiddleware()
{
    basicAuth.setUsername("admin");
    basicAuth.setPassword(config.getString("adm_pass"));
    basicAuth.setRealm("MyApp");
    basicAuth.setAuthFailureMessage("Authentication failed");
    basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    basicAuth.generateHash(); // precompute hash (optional but recommended)
}

void ReServer::setHandlers()
{
    // serve your home page here
    on("/", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::handleRoot, this, std::placeholders::_1))
        .setFilter([this](__unused AsyncWebServerRequest *request)
                   { return espConnect->getState() != Mycila::ESPConnect::State::PORTAL_STARTED; });

    // serve not found page
    onNotFound(std::bind(&ReServer::handleNotFound, this, std::placeholders::_1));

    on("/heap", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::heapHandler, this, std::placeholders::_1));

    on("admin/clear", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminClearHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on(AsyncURIMatcher::exact("/admin"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on(AsyncURIMatcher::exact("/admin/settings"), HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminSettingsGetHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on(AsyncURIMatcher::exact("/admin/settings"), HTTP_POST, std::bind(&ReServer::adminSettingsPostHandler, this, std::placeholders::_1, std::placeholders::_2))
        .addMiddleware(&basicAuth);

    on("/admin/restart", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminRestartHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on("/admin/safeboot", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminSafebootHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on("/admin/decomission", HTTP_GET, (ArRequestHandlerFunction)std::bind(&ReServer::adminDecommissionHandler, this, std::placeholders::_1))
        .addMiddleware(&basicAuth);

    on("/switchbot/press", HTTP_GET | HTTP_POST, (ArRequestHandlerFunction)std::bind(&ReServer::switchbotPressHandler, this, std::placeholders::_1));

    on("/switchbot/command", HTTP_GET | HTTP_POST, (ArRequestHandlerFunction)std::bind(&ReServer::switchbotCommandHandler, this, std::placeholders::_1));
}

void ReServer::handleRoot(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "BLE and Matter ESP32 Gateway to Switchbot Bot");
}

void ReServer::handleNotFound(AsyncWebServerRequest *request)
{
    String message = "Invalid Url\n\n";
    message += "URI: ";
    message += request->url();
    message += "\nMethod: ";
    message += (request->method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += request->args();
    message += "\n";
    for (uint8_t i = 0; i < request->args(); i++)
    {
        message += " " + request->argName(i) + ": " + request->arg(i) + "\n";
    }
    request->send(404, "text/plain", message);
}

void ReServer::heapHandler(AsyncWebServerRequest *request)
{
    JsonDocument doc;
    doc["Heap_size"] = ESP.getHeapSize();
    doc["Free_heap"] = ESP.getFreeHeap();
    doc["Min_Free_Heap"] = ESP.getMinFreeHeap();
    doc["Max_Alloc_Heap"] = ESP.getMaxAllocHeap();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void ReServer::adminClearHandler(AsyncWebServerRequest *request)
{
    espConnect->clearConfiguration();
    config.clear();
    request->send(200);

    ESP.restart();
}

void ReServer::adminHandler(AsyncWebServerRequest *request)
{
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t *)(settings_html_start), settings_html_end - settings_html_start);
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

void ReServer::adminSettingsGetHandler(AsyncWebServerRequest *request)
{
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonObject doc = response->getRoot().to<JsonObject>();

    doc["network"]["ssid"] = config.getString("net_ssid");
    doc["network"]["password"] = config.getString("net_pass");
    doc["device"]["port_web"] = config.get<int>("dev_port");
    doc["device"]["matter"] = config.get<bool>("dev_matter");
    doc["mqtt"]["enable"] = config.get<bool>("mqtt_en");
    doc["mqtt"]["ip"] = config.getString("mqtt_ip");
    doc["mqtt"]["port"] = config.get<int>("mqtt_port");
    doc["mqtt"]["username"] = config.getString("mqtt_user");
    doc["mqtt"]["password"] = config.getString("mqtt_pass");
    doc["bot"]["mac"] = config.getString("bot_mac");
    doc["bot"]["scantime"] = config.get<int>("bot_scantime");
    doc["bot"]["txpower"] = config.get<int>("bot_txpower");
    doc["admin"]["password"] = config.getString("adm_pass");
    doc["admin"]["webserial"] = config.get<bool>("adm_webserial");

    response->setLength();
    request->send(response);
}

void ReServer::adminSettingsPostHandler(AsyncWebServerRequest *request, JsonVariant &json)
{
    JsonObject doc = json.as<JsonObject>();

    config.setString("net_ssid", doc["network"]["ssid"].as<const char *>());
    config.setString("net_pass", doc["network"]["password"].as<const char *>());
    config.set<int>("dev_port", doc["device"]["port_web"].as<int>());
    config.set<bool>("dev_matter", doc["device"]["matter"].as<bool>());
    config.set<bool>("mqtt_en", doc["mqtt"]["enable"].as<bool>());
    config.setString("mqtt_ip", doc["mqtt"]["ip"].as<const char *>());
    config.set<int>("mqtt_port", doc["mqtt"]["port"].as<int>());
    config.setString("mqtt_user", doc["mqtt"]["username"].as<const char *>());
    config.setString("mqtt_pass", doc["mqtt"]["password"].as<const char *>());
    config.setString("bot_mac", doc["bot"]["mac"].as<const char *>());
    config.set<int>("bot_scantime", doc["bot"]["scantime"].as<int>());
    config.set<int>("bot_txpower", doc["bot"]["txpower"].as<int>());
    config.setString("adm_pass", doc["admin"]["password"].as<const char *>());
    config.set<bool>("adm_webserial", doc["admin"]["webserial"].as<bool>());

    serializeJson(doc, Serial);

    request->send(200, "application/json", "{}");
}

void ReServer::adminRestartHandler(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "Device has been restarted");
    ESP.restart();
}

void ReServer::adminSafebootHandler(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "Restarting in SafeBoot mode...");
    Mycila::System::restartFactory("safeboot", 1000);
}

void ReServer::adminDecommissionHandler(AsyncWebServerRequest *request)
{
    logger.debug(RE_TAG, "Decommissioning the Plugin Matter Accessory. It shall be commissioned again");

    Matter.decommission();
    request->send(200, "text/plain", "Decommissioning the Matter Accessory. It shall be commissioned again");
}

void ReServer::switchbotPressHandler(AsyncWebServerRequest *request)
{
    ctx.setDoCommand(BOT_PRESS_COMMAND);

    if (ctx.getBleDeviceFound())
    {
        ctx.setDoConnect(true);
        pressRequest = request->pause();
    }
    else
    {
        request->send(200, "text/plain", "Device is not connected, command NOT executed...");
    }
}

void ReServer::switchbotCommandHandler(AsyncWebServerRequest *request)
{
    String code;

    if (request->hasParam("cmd") && !request->getParam("cmd")->value().isEmpty())
    {
        code = request->getParam("cmd")->value();
        ctx.setDoCommand(code.c_str());

        if (ctx.getBleDeviceFound())
        {
            ctx.setDoConnect(true);
            pressRequest = request->pause();
        }
        else
        {
            request->send(200, "text/plain", "Device is not connected, command NOT executed");
        }
    }
    else
    {
        request->send(200, "text/plain", "Missing parameter");
    }
}
