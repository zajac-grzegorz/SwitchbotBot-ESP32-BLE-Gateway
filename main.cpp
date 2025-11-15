#include <Arduino.h>
#include <M5AtomS3.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "ReBLEUtils.h"
#include "ReBLEConfig.h"

std::string botMacAddr = botMac;

WebServer server(80);

static const NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEScan* pScan = nullptr;

static bool doConnect = false;
static uint32_t scanTimeMs = 5000; /** scan time in milliseconds, 0 = scan forever */

static BLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID controlCharacteristicUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID notifyCharacteristicUUID("cba20003-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID descriptorUUID("2902");

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ClientCallbacks : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient) override
    {
        Serial.printf("Connected\n");
    }

    void onDisconnect(NimBLEClient *pClient, int reason) override
    {
        Serial.printf("%s Disconnected, reason = %d - Starting scan\n", pClient->getPeerAddress().toString().c_str(), reason);
        // NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }

    /********************* Security handled here *********************/
    void onPassKeyEntry(NimBLEConnInfo &connInfo) override
    {
        Serial.printf("Server Passkey Entry\n");
        /**
         * This should prompt the user to enter the passkey displayed
         * on the peer device.
         */
        NimBLEDevice::injectPassKey(connInfo, 123456);
    }

    void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pass_key) override
    {
        Serial.printf("The passkey YES/NO number: %" PRIu32 "\n", pass_key);
        /** Inject false if passkeys don't match. */
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }

    /** Pairing process complete, we can check the results in connInfo */
    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
    {
        if (!connInfo.isEncrypted())
        {
            Serial.printf("Encrypt connection failed - disconnecting\n");
            /** Find the client with the connection handle provided in connInfo */
            NimBLEDevice::getClientByHandle(connInfo.getConnHandle())->disconnect();
            return;
        }
    }
} clientCallbacks;

/** Define a class to handle the callbacks when scan events are received */
class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        if (advertisedDevice->getAddress().toString() == botMacAddr)
        {
            Serial.printf("Advertised Device found: %s\n", advertisedDevice->getAddress().toString().c_str());

            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();

            /** Save the device reference in a global for the client to use*/
            advDevice = advertisedDevice;

            /** Ready to connect now */
            doConnect = true;
        }
    }

    /** Callback to process the results of the completed scan or restart it */
    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        Serial.printf("Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
        NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }
} scanCallbacks;

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    std::string str = (isNotify == true) ? "Notification" : "Indication";
    str += " from ";
    str += pRemoteCharacteristic->getClient()->getPeerAddress().toString();
    Serial.printf("%s\n", str.c_str());

    str = "* Service = " + pRemoteCharacteristic->getRemoteService()->getUUID().toString();
    Serial.printf("%s\n", str.c_str());

    str = "** Characteristic = " + pRemoteCharacteristic->getUUID().toString();
    Serial.printf("%s\n", str.c_str());

    str = "*** Value = " + NimBLEUtils::dataToHexString(pData, length);
    Serial.printf("%s\n", str.c_str());
}

/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToServer()
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
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());

        if (pClient)
        {
            if (!pClient->connect(advDevice, false))
            {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
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
            Serial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();

        Serial.printf("New client created\n");

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

        if (!pClient->connect(advDevice))
        {
            /** Created a client but failed to connect, don't need to keep it as it has no data */
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected())
    {
        if (!pClient->connect(advDevice))
        {
            Serial.printf("Failed to connect\n");
            return false;
        }
    }

    Serial.printf("Connected to: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

    /** Now we can read/write/subscribe the characteristics of the services we are interested in */
    NimBLERemoteService *pSvc = nullptr;
    NimBLERemoteCharacteristic *pChr = nullptr;
    NimBLERemoteDescriptor *pDsc = nullptr;

    // pSvc = pClient->getService(serviceUUID);
    pSvc = pClient->getService(serviceUUID);

    if (pSvc)
    {
        // Type: notify
        pChr = pSvc->getCharacteristic(notifyCharacteristicUUID);

        if (pChr)
        {
            if (pChr->canRead())
            {
                Serial.printf("%s Value: %s\n", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
            }

            pDsc = pChr->getDescriptor(descriptorUUID);
            if (pDsc)
            {
                Serial.printf("Descriptor: %s  Value: %s\n", pDsc->getUUID().toString().c_str(), pDsc->readValue().c_str());
            }

            if (pChr->canNotify())
            {
                if (!pChr->subscribe(true, notifyCB))
                {
                    pClient->disconnect();
                    return false;
                }

                Serial.println("Notifications have been set");
            }
            else if (pChr->canIndicate())
            {
                /** Send false as first argument to subscribe to indications instead of notifications */
                if (!pChr->subscribe(false, notifyCB))
                {
                    pClient->disconnect();
                    return false;
                }

                Serial.println("Indications have been set");
            }
        }
    }
    else
    {
        Serial.println("SwitchBot Bot service not found");
    }

    if (pSvc)
    {
        // Type: write, write without response
        pChr = pSvc->getCharacteristic(controlCharacteristicUUID);

        if (pChr)
        {
            if (pChr->canRead())
            {
                Serial.printf("%s Value: %s\n", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
            }

            if (pChr->canWrite())
            {
                // std::vector<uint8_t> vPress { 0x57, 0x01, 0x00 };
                std::vector<uint8_t> vPress = stringToHexArray("570100");
                Serial.printf("Command data: %s\n", NimBLEUtils::dataToHexString(vPress.data(), vPress.size()).c_str());
                // byte bArrayPress[] = {0x57, 0x01, 0x00};

                // if (pChr->writeValue(bArrayPress, 3, true))
                if (pChr->writeValue(vPress, true))
                {
                    Serial.printf("Wrote new value to: %s\n", pChr->getUUID().toString().c_str());
                }
                else
                {
                    pClient->disconnect();
                    return false;
                }

                if (pChr->canRead())
                {
                    Serial.printf("The value of: %s is now: %s\n", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
                }
            }
        }
    }
    else
    {
        Serial.println("Switchbot Bot service not found.");
    }

    Serial.println("Done with this device!");
    
    return true;
}

void handleRoot() 
{
    server.send(200, "text/plain", "ESP32 to Switchbot Bot gateway");
}

void handleNotFound()
{
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void setup()
{
    Serial.begin(115200);
    // delay(3000);

    AtomS3.begin(true);
    AtomS3.dis.setBrightness(50);
    AtomS3.dis.drawpix(0x00DD00);
    AtomS3.update();

    Serial.printf("Starting NimBLE Client\n");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.println("");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to: ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("esp32-switchbot"))
    {
        Serial.println("MDNS responder started");
    }

    server.on("/", handleRoot);

    server.on("/start", []()
    {
        /** Start scanning for advertisers */
        pScan->start(scanTimeMs);

        Serial.printf("Scanning for peripherals\n");
        server.send(200, "text/plain", "Scanning started"); 
    });

    server.on("/open", []()
    {
        if (advDevice && !doConnect)
        {
            doConnect = true;
        }

        server.send(200, "text/plain", "Open init"); 
    });

    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("HTTP server started");

    /** Initialize NimBLE and set the device name */
    NimBLEDevice::init("SwitchBot-Bot-Client");

    NimBLEDevice::whiteListAdd(NimBLEAddress(botMacAddr, 0));
    /**
     * Set the IO capabilities of the device, each option will trigger a different pairing method.
     *  BLE_HS_IO_KEYBOARD_ONLY   - Passkey pairing
     *  BLE_HS_IO_DISPLAY_YESNO   - Numeric comparison pairing
     *  BLE_HS_IO_NO_INPUT_OUTPUT - DEFAULT setting - just works pairing
     */
    // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY); // use passkey
    // NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); //use numeric comparison

    /**
     * 2 different ways to set security - both calls achieve the same result.
     *  no bonding, no man in the middle protection, BLE secure connections.
     *  These are the default values, only shown here for demonstration.
     */
    // NimBLEDevice::setSecurityAuth(false, false, true);
    // NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC);

    /** Optional: set the transmit power */
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** 3dbm */
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

void loop()
{
    /** Loop here until we find a device we want to connect to */
    delay(10);

    if (doConnect)
    {
        doConnect = false;
        /** Found a device we want to connect to, do it now */
        if (connectToServer())
        {
            Serial.printf("Success! we should now be getting notifications, scanning for more!\n");
        }
        else
        {
            Serial.printf("Failed to connect, starting scan\n");
        }

        // NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }

    server.handleClient();
}