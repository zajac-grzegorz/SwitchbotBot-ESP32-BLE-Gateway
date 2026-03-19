#pragma once

#include <NimBLEDevice.h>
#include "ReCommon.h"
#include "ReContext.h"

static BLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID controlCharacteristicUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID notifyCharacteristicUUID("cba20003-224d-11e6-9fb8-0002a5d5c51b");

class ReClientCallbacks : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient) override;
    void onDisconnect(NimBLEClient *pClient, int reason) override;

private:
    uint64_t conTimeout = 0;
};

/** Define a class to handle the callbacks when scan events are received */
class ReScanCallbacks : public NimBLEScanCallbacks
{
public:
    const NimBLEAdvertisedDevice *getAdvDevice() const { return advDevice; }

private:
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;

    /** Callback to process the results of the completed scan or restart it */
    void onScanEnd(const NimBLEScanResults &results, int reason) override;

    ReContext ctx;
    const NimBLEAdvertisedDevice *advDevice = nullptr;
};

class ReBLEDevice
{
public:
    typedef std::function<void(std::string&)> BleDataCallback;

    void initialize(BleDataCallback callback);
    void start();
    bool executeSwitchBotCommand(std::string cmd);

private:
    void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify);
    bool connectToSwitchBot();

    ReClientCallbacks clientCallbacks;
    ReScanCallbacks scanCallbacks;
    BleDataCallback bleDataCallback { nullptr };

    NimBLEScan* pScan = nullptr;
    std::string resultData;

};