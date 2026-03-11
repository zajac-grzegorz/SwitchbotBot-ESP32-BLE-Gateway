#include "ReContext.h"

PsychicMqttClient ReContext::mqttClient;
AsyncWebServer* ReContext::server = nullptr;
// AsyncWebServerRequestPtr ReContext::pressRequest;
// const AsyncAuthenticationMiddleware ReContext::basicAuth;
Mycila::ESPConnect* ReContext::espConnect = nullptr;
// const NimBLEAdvertisedDevice* ReContext::advDevice = nullptr;
// const NimBLEScan* ReContext::pScan = nullptr;
bool ReContext::doConnect = false;
std::string ReContext::doCommand = "570100"; // default is press command
MatterOnOffPlugin ReContext::OnOffPlugin;

