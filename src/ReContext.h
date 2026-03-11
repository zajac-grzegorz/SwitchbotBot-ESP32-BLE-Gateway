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
    // constructor
    ReContext() = default;
    
    PsychicMqttClient& getMqttClient() {
        return mqttClient;
    }
    
    // const AsyncWebServer* getServer() {
    //     return server;
    // }
    
    // AsyncWebServerRequestPtr getPressRequest() {
    //     return pressRequest;
    // }
    
    // const AsyncAuthenticationMiddleware& getBasicAuth() {
    //     return basicAuth;
    // }
    
    // const Mycila::ESPConnect* getEspConnect() {
    //     return espConnect;
    // }
    
    // const NimBLEAdvertisedDevice* getAdvDevice() {
    //     return advDevice;
    // }
    
    // const NimBLEScan* getPScan() {
    //     return pScan;
    // }
    
    bool getDoConnect() {
        return doConnect;
    }

    void setDoConnect(bool value) {
        doConnect = value;
    }
    
    std::string getDoCommand() {
        return doCommand;
    }

    void setDoCommand(const std::string& value) {
        doCommand = value;
    }
    
    MatterOnOffPlugin& getOnOffPlugin() {
        return OnOffPlugin;
    }


    private:

    static PsychicMqttClient mqttClient;
    // static const AsyncWebServer* server;
    // static AsyncWebServerRequestPtr pressRequest;
    
    // // basicAuth
    // static const AsyncAuthenticationMiddleware basicAuth;

    // static const Mycila::ESPConnect* espConnect;

    // static const NimBLEAdvertisedDevice* advDevice;
    // static const NimBLEScan* pScan;

    // // these can be inline global variables
    static bool doConnect;
    static std::string doCommand;
    static MatterOnOffPlugin OnOffPlugin;
};
    