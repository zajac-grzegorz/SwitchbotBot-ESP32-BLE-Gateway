#include "ReBLEDevice.h"
#include "ReBLEUtils.h"
#include "ReLED.h"

void ReClientCallbacks::onConnect(NimBLEClient *pClient)
{
    conTimeout = millis();

    // LED_COLOR_UPDATE(LED_COLOR_RED);
    // LED_STATUS_UPDATE(start(LED_AP_CONNECTED));
}

void ReClientCallbacks::onDisconnect(NimBLEClient *pClient, int reason)
{
    uint64_t tm = millis() - conTimeout;

    logger.info(RE_TAG, "%s Disconnected, reason = %d, timeout = %lld",
                pClient->getPeerAddress().toString().c_str(), reason, tm);

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(on());
}

void ReScanCallbacks::onResult(const NimBLEAdvertisedDevice *advertisedDevice)
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
void ReScanCallbacks::onScanEnd(const NimBLEScanResults &results, int reason)
{
    logger.info(RE_TAG, "Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
    int scanTimeMs = config.get<int>("bot_scantime");
    NimBLEDevice::getScan()->start(scanTimeMs, false, true);

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
}

void ReBLEDevice::initialize(BleDataCallback callback)
{
    bleDataCallback = callback;

    /** Initialize NimBLE and set the device name */
    NimBLEDevice::init("SwitchBot-Bot-Client");
    NimBLEDevice::whiteListAdd(NimBLEAddress(config.getString("bot_mac"), 0));
    NimBLEDevice::setPower((esp_power_level_t)config.get<int>("bot_txpower"));

    logger.debug(RE_TAG, "BLE power Tx level: %ld", config.get<int>("bot_txpower"));

    pScan = NimBLEDevice::getScan();

    /** Set the callbacks to call when scan events occur, no duplicates */
    pScan->setScanCallbacks(&scanCallbacks, false);

    /** Set scan interval (how often) and window (how long) in milliseconds */
    pScan->setInterval(70);
    pScan->setWindow(40);

    /**
     * Active scan will gather scan response data from advertisers
     *  but will use more energy from both devices
     */
    // Do not need active scan, only mac address is important during advertisment process
    // pScan->setActiveScan(true);
}

void ReBLEDevice::start()
{
    /** Start scanning for advertisers */ // move this to matter event handler?
    int scanTimeMs = config.get<int>("bot_scantime");
    pScan->start(scanTimeMs);
}

void ReBLEDevice::notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    std::string str = (isNotify == true) ? "Notification" : "Indication";
    str += " from ";
    str += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
    logger.debug(RE_TAG, "%s", str.c_str());

    str = "* Service = " + pRemoteCharacteristic->getRemoteService()->getUUID().toString();
    logger.debug(RE_TAG, "%s", str.c_str());

    str = "** Characteristic = " + pRemoteCharacteristic->getUUID().toString();
    logger.debug(RE_TAG, "%s", str.c_str());

    std::string resultData = NimBLEUtils::dataToHexString(pData, length);

    str = "*** Value = " + resultData;
    logger.info(RE_TAG, "%s", str.c_str());

    // call the callback from main.cpp to update the state of the plugin
    if (bleDataCallback)
    {
        bleDataCallback(resultData);
    }
}

/** Handles the provisioning of clients and connects / interfaces with the server */
bool ReBLEDevice::connectToSwitchBot()
{
    NimBLEClient *pClient = nullptr;

    /** Check if we have a client we should reuse first **/
    if (NimBLEDevice::getCreatedClientCount())
    {
        /**
         *  Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(scanCallbacks.getAdvDevice()->getAddress());

        if (pClient)
        {
            if (pClient->isConnected())
            {
                logger.info(RE_TAG, "Client exists and is already conected");
                return true;
            } 
            else
            {
                if (!pClient->connect(scanCallbacks.getAdvDevice(), false))
                {
                    logger.error(RE_TAG, "Reconnect failed");
                    return false;
                }

                logger.info(RE_TAG, "Reconnected client");
            }
        }
        else
        {
            /**
             *  We don't already have a client that knows this device,
             *  check for a client that is disconnected that we can use.
             */
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    /** No client to reuse? Create a new one. */
    if (!pClient)
    {
        if (NimBLEDevice::getCreatedClientCount() >= MYNEWT_VAL(BLE_MAX_CONNECTIONS))
        {
            logger.error(RE_TAG, "Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();

        logger.info(RE_TAG, "New client created");

        pClient->setClientCallbacks(&clientCallbacks, false);
        /**
         *  Set initial connection parameters:
         *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
         *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
         *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 150 * 10ms = 1500ms timeout
         */
        pClient->setConnectionParams(12, 12, 0, 150);

        /** Set how long we are willing to wait for the connection to complete (milliseconds), default is 30000. */
        pClient->setConnectTimeout(5 * 1000);

        if (!pClient->connect(scanCallbacks.getAdvDevice()))
        {
            /** Created a client but failed to connect, don't need to keep it as it has no data */
            NimBLEDevice::deleteClient(pClient);

            logger.error(RE_TAG, "Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected())
    {
        if (!pClient->connect(scanCallbacks.getAdvDevice()))
        {
            logger.error(RE_TAG, "Failed to connect");
            return false;
        }
    }

    logger.info(RE_TAG, "Connected to: %s RSSI: %d", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

    /** Now we can read/write/subscribe the characteristics of the services we are interested in */
    NimBLERemoteService *pSvc = nullptr;
    NimBLERemoteCharacteristic *pChr = nullptr;

    pSvc = pClient->getService(serviceUUID);

    if (pSvc)
    {
        // Type: notify
        pChr = pSvc->getCharacteristic(notifyCharacteristicUUID);

        if (pChr)
        {
            if (pChr->canRead())
            {
                logger.debug(RE_TAG, "%s Value: %s", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
            }

            if (pChr->canNotify())
            {
                // if (!pChr->subscribe(true, notifyCB))
                if (!pChr->subscribe(true, std::bind(&ReBLEDevice::notifyCB, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)))
                {
                    pClient->disconnect();
                    return false;
                }

                logger.info(RE_TAG, "Notifications have been set");
            }
            else if (pChr->canIndicate())
            {
                /** Send false as first argument to subscribe to indications instead of notifications */
                // if (!pChr->subscribe(false, notifyCB))
                if (!pChr->subscribe(false, std::bind(&ReBLEDevice::notifyCB, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)))
                {
                    pClient->disconnect();
                    return false;
                }

                logger.debug(RE_TAG, "Indications have been set");
            }
        }
    }
    else
    {
        logger.error(RE_TAG, "SwitchBot Bot service not found");
    }

    logger.info(RE_TAG, "Connected, subscribed to notifications and waiting for a command...");

    return true;
}

bool ReBLEDevice::executeSwitchBotCommand(std::string cmd)
{
    // Establish connection with the client
    if (!connectToSwitchBot())
    {
        logger.error(RE_TAG, "executeSwitchBotCommand: Client not created");
        return false;
    }

    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(scanCallbacks.getAdvDevice()->getAddress());
    
    if (!pClient)
    {
        logger.error(RE_TAG, "executeSwitchBotCommand: Client not exists");
        return false;
    }
    else
    {
        if (!pClient->isConnected())
        {
            logger.error(RE_TAG, "executeSwitchBotCommand: Client not connected");
            return false;
        }
    }

    NimBLERemoteService *pSvc = nullptr;
    NimBLERemoteCharacteristic *pChr = nullptr;

    pSvc = pClient->getService(serviceUUID);

    if (pSvc)
    {
        // Type: write, write without response
        pChr = pSvc->getCharacteristic(controlCharacteristicUUID);

        if (pChr)
        {
            if (pChr->canRead())
            {
                logger.debug(RE_TAG, "%s Value: %s", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
            }

            if (pChr->canWrite())
            {
                std::vector<uint8_t> vPress = stringToHexArray(cmd);
                logger.info(RE_TAG, "Command data: %s", NimBLEUtils::dataToHexString(vPress.data(), vPress.size()).c_str());

                // All command for Bot must start with 0x57 byte
                if (vPress.at(0) != 0x57)
                {
                    logger.error(RE_TAG, "Write failed - command must start with 0x57 byte");
                    return false;
                }

                if (pChr->writeValue(vPress, true))
                {
                    logger.info(RE_TAG, "Wrote new value to: %s", pChr->getUUID().toString().c_str());
                }
                else
                {
                    pClient->disconnect();
                    return false;
                }

                if (pChr->canRead())
                {
                    logger.info(RE_TAG, "The value of: %s is now: %s", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
                }
            }
        }
    }
    else
    {
        logger.error(RE_TAG, "Switchbot Bot service not found.");
    }

    return true;
}

