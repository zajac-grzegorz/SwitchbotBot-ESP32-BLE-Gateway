#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <Matter.h>
#include <NimBLEDevice.h>
#include "ReBLEUtils.h"
#include <PsychicMqttClient.h>

class ReContext
{
    public:

    ReContext() = default;
    
    PsychicMqttClient& getMqttClient() {
        return mqttClient;
    }
    
    // AsyncWebServerRequestPtr getPressRequest() {
    //     return pressRequest;
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
    
    std::string& getDoCommand() {
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
    // static AsyncWebServerRequestPtr pressRequest;
    

    // static const NimBLEAdvertisedDevice* advDevice;
    // static const NimBLEScan* pScan;

    static bool doConnect;
    static std::string doCommand;
    static MatterOnOffPlugin OnOffPlugin;
};
    