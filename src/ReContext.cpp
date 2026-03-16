#include "ReContext.h"

PsychicMqttClient ReContext::mqttClient;
// AsyncWebServerRequestPtr ReContext::pressRequest;
// const NimBLEAdvertisedDevice* ReContext::advDevice = nullptr;
// const NimBLEScan* ReContext::pScan = nullptr;
bool ReContext::doConnect = false;
std::string ReContext::doCommand = "570100"; // default is press command
MatterOnOffPlugin ReContext::OnOffPlugin;

