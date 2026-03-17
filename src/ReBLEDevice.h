#pragma once

#include <NimBLEDevice.h>
#include "ReCommon.h"
#include "ReContext.h"
#include "ReLED.h"

class ReClientCallbacks : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient) override
    {
        conTimeout = millis();

        // LED_COLOR_UPDATE(LED_COLOR_RED);
        // LED_STATUS_UPDATE(start(LED_AP_CONNECTED));
    }

    void onDisconnect(NimBLEClient *pClient, int reason) override
    {
        uint64_t tm = millis() - conTimeout;

        logger.info(RE_TAG, "%s Disconnected, reason = %d, timeout = %lld",
                    pClient->getPeerAddress().toString().c_str(), reason, tm);

        LED_COLOR_UPDATE(LED_COLOR_GREEN);
        LED_STATUS_UPDATE(on());
    }

private:
    uint64_t conTimeout = 0;
};

/** Define a class to handle the callbacks when scan events are received */
class ReScanCallbacks : public NimBLEScanCallbacks
{
public:
    const NimBLEAdvertisedDevice *getAdvDevice() const { return advDevice; }

private:
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        // This is a device with our MAC address
        std::string botMacAddr = config.getString("bot_mac");
        // Convert to lowercase because NimBLE returns mac address in lowercase
        std::transform(botMacAddr.begin(), botMacAddr.end(), botMacAddr.begin(), ::tolower);

        if (advertisedDevice->getAddress().toString() == botMacAddr)
        {
            logger.info(RE_TAG, "Advertised Device found: %s", advertisedDevice->getAddress().toString().c_str());

            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();

            /** Save the device reference in a global for the client to use*/
            advDevice = advertisedDevice;

            ctx.setBleDeviceFound(true);

            LED_COLOR_UPDATE(LED_COLOR_GREEN);
            LED_STATUS_UPDATE(on());
        }
    }

    /** Callback to process the results of the completed scan or restart it */
    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        logger.info(RE_TAG, "Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
        int scanTimeMs = config.get<int>("bot_scantime");
        NimBLEDevice::getScan()->start(scanTimeMs, false, true);

        LED_COLOR_UPDATE(LED_COLOR_GREEN);
        LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
    }

    ReContext ctx;
    const NimBLEAdvertisedDevice *advDevice = nullptr;
};