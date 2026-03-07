#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <MycilaESPConnect.h>
#include <Matter.h>
#include <NimBLEDevice.h>
#include "ReBLEUtils.h"
#include <PsychicMqttClient.h>

class ReContext
{
    public:

    static PsychicMqttClient mqttClient;
    static AsyncWebServer* server;
    static AsyncWebServerRequestPtr pressRequest;
    
    // basicAuth
    static AsyncAuthenticationMiddleware basicAuth;

    static Mycila::ESPConnect* espConnect;

    static const NimBLEAdvertisedDevice* advDevice;
    static NimBLEScan* pScan;

    // these can be inline global variables
    static bool doConnect;
    static std::string doCommand;
    static MatterOnOffPlugin OnOffPlugin;
};
    