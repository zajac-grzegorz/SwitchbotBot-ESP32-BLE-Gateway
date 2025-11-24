#include <Arduino.h>
#include <M5AtomS3.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MycilaESPConnect.h>
#include <ESPmDNS.h>
#include "ReBLEUtils.h"
#include "ReBLEConfig.h"
#include <Matter.h>
#include <MycilaSystem.h>

MatterOnOffPlugin OnOffPlugin;

std::string botMacAddr = botMac;

static AsyncWebServer server(webServerPort);
static Mycila::ESPConnect espConnect(server);
static AsyncWebServerRequestPtr pressRequest;

static const NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEScan* pScan = nullptr;

static bool doConnect = false;
static std::string doCommand = "570100"; // default is press command
static uint64_t conTimeout = 0;
static uint32_t scanTimeMs = 5000; /** scan time in milliseconds, 0 = scan forever */

static BLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID controlCharacteristicUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID notifyCharacteristicUUID("cba20003-224d-11e6-9fb8-0002a5d5c51b");

void ledOn(CRGB color, uint8_t brightness, bool init=false);

class ClientCallbacks : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient *pClient) override
    {
        conTimeout = millis();

        ledOn(0xDD0000, 50);
    }

    void onDisconnect(NimBLEClient *pClient, int reason) override
    {
        uint64_t tm = millis() - conTimeout;

        Serial.printf("%s Disconnected, reason = %d, timeout = %lld\n", 
            pClient->getPeerAddress().toString().c_str(), reason, tm);
        
        ledOn(0x00DD00, 50);
    }
} clientCallbacks;

/** Define a class to handle the callbacks when scan events are received */
class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        // This is a device with our MAC address
        if (advertisedDevice->getAddress().toString() == botMacAddr)
        {
            Serial.printf("Advertised Device found: %s\n", advertisedDevice->getAddress().toString().c_str());

            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();

            /** Save the device reference in a global for the client to use*/
            advDevice = advertisedDevice;
            // doConnect = true;
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

    std::string resultData = NimBLEUtils::dataToHexString(pData, length);

    str = "*** Value = " + resultData;
    Serial.printf("%s\n", str.c_str());

    if (auto request = pressRequest.lock()) 
    {
        JsonDocument doc;
        doc["status"] = resultData.substr(0, 2);
        doc["payload"] = resultData.substr(2);
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    } 

    OnOffPlugin.setOnOff(false);
    OnOffPlugin.updateAccessory();
}

/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToSwitchBot()
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
            if (pClient->isConnected())
            {
                Serial.println("Client exists and is already conected");
                return true;
            } 
            else
            {
                if (!pClient->connect(advDevice, false))
                {
                    Serial.println("Reconnect failed");
                    return false;
                }

                Serial.println("Reconnected client");
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
            Serial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();

        Serial.println("New client created");

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
            Serial.println("Failed to connect");
            return false;
        }
    }

    Serial.printf("Connected to: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

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
                Serial.printf("%s Value: %s\n", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
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

    Serial.println("Connected, subscribed to notifications and waiting for a command...");

    return true;
}

bool executeSwitchBotCommand(std::string cmd)
{
    // Establish connection with the client
    if (!connectToSwitchBot())
    {
        Serial.println("executeSwitchBotCommand: Client not created");
        return false;
    }

    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    
    if (!pClient)
    {
        Serial.println("executeSwitchBotCommand: Client not exists");
        return false;
    }
    else
    {
        if (!pClient->isConnected())
        {
            Serial.println("executeSwitchBotCommand: Client not connected");
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
                Serial.printf("%s Value: %s\n", pChr->getUUID().toString().c_str(), pChr->readValue().c_str());
            }

            if (pChr->canWrite())
            {
                std::vector<uint8_t> vPress = stringToHexArray(cmd);
                Serial.printf("Command data: %s\n", NimBLEUtils::dataToHexString(vPress.data(), vPress.size()).c_str());

                // All command for Bot must start with 0x57 byte
                if (vPress.at(0) != 0x57)
                {
                    Serial.println("Write failed - command must start with 0x57 byte");
                    return false;
                }

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

    return true;
}

void handleRoot(AsyncWebServerRequest *request) 
{
    request->send(200, "text/plain", "BLE and Matter ESP32 Gateway to Switchbot Bot");
}

void handleNotFound(AsyncWebServerRequest *request)
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

void ledOn(CRGB color, uint8_t brightness, bool init)
{
    if (init)
    {
        AtomS3.begin(init);
    }

    AtomS3.dis.setBrightness(brightness);
    AtomS3.dis.drawpix(color);
    AtomS3.update();
}

// Matter Protocol Endpoint Callback
bool setPluginOnOff(bool state) {
    Serial.printf("User Callback :: New Plugin State = %s\r\n", state ? "ON" : "OFF");
  
    doCommand = "570100";

    if (state && advDevice)
    {
        doConnect = true;
    }

    return true;
}

void setup()
{
    Serial.begin(115200);

    ledOn(0x0000DD, 50, true);

    Serial.println("Starting BLE and Matter ESP32 Gateway to Switchbot Bot");

    // network state listener
    espConnect.listen([](__unused Mycila::ESPConnect::State previous, __unused Mycila::ESPConnect::State state) 
    {
        JsonDocument doc;
        espConnect.toJson(doc.to<JsonObject>());
        serializeJsonPretty(doc, Serial);
        Serial.println();
    });

    espConnect.setAutoRestart(true);
    espConnect.setBlocking(true);
    Serial.println("Trying to connect to saved WiFi or will start portal...");
    espConnect.begin("BLEGateway", "BLEGateway");
    Serial.println("ESPConnect completed, continuing setup()...");

    // serve your home page here
    server.on("/", handleRoot).setFilter([](__unused AsyncWebServerRequest* request) 
    { 
        return espConnect.getState() != Mycila::ESPConnect::State::PORTAL_STARTED; 
    });
    
    // if (MDNS.begin("esp32-switchbot"))
    // {
    //     Serial.println("MDNS responder started");
    // }

    server.on("/", handleRoot);

    // clear persisted config
    server.on("/admin/clear", HTTP_GET, [&](AsyncWebServerRequest* request) 
    {
        espConnect.clearConfiguration();
        request->send(200);
        ESP.restart();
    });

    server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        JsonDocument doc;
        doc["Heap_size"] = ESP.getHeapSize();
        doc["Free_heap"] = ESP.getFreeHeap();
        doc["Min_Free_Heap"] = ESP.getMinFreeHeap();
        doc["Max_Alloc_Heap"] = ESP.getMaxAllocHeap();
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    server.on("/admin/restart", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/plain", "Device has been restarted");
        ESP.restart();
    });

    server.on("/admin/safeboot", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", "<form method='POST' action='/admin/safeboot' enctype='multipart/form-data'><input type='submit' value='Restart in SafeBoot mode'></form>");
    });

    server.on("/admin/safeboot", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/plain", "Restarting in SafeBoot mode...");
        Mycila::System::restartFactory("safeboot", 1000);
    });

    server.on("/admin/decomission", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Serial.println("Decommissioning the Plugin Matter Accessory. It shall be commissioned again");
        OnOffPlugin.setOnOff(false);
        Matter.decommission();

        request->send(200, "text/plain", "Decommissioning the Matter Accessory. It shall be commissioned again");
    });

    server.on("/switchbot/press", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        doCommand = "570100";

        if (advDevice)
        {
            doConnect = true;
            pressRequest = request->pause();
        }
        else
        {
            request->send(200, "text/plain", "Device is not connected, command NOT executed...");
        }
    });

    server.on("/switchbot/press", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        doCommand = "570100";

        if (advDevice)
        {
            doConnect = true;
            pressRequest = request->pause();
        }
        else
        {
            request->send(200, "text/plain", "Device is not connected, command NOT executed...");
        }
    });

    server.on("/switchbot/command", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        String code;

        if (request->hasParam("cmd") && !request->getParam("cmd")->value().isEmpty())
        {
            code = request->getParam("cmd")->value();
            doCommand = code.c_str();

            if (advDevice)
            {
                doConnect = true;
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
    });


    server.onNotFound(handleNotFound);
    server.begin();

    Serial.println("Async Web Server started");

    /** Initialize NimBLE and set the device name */
    NimBLEDevice::init("SwitchBot-Bot-Client");
    NimBLEDevice::whiteListAdd(NimBLEAddress(botMacAddr, 0));
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9dbm */

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

    OnOffPlugin.begin();
    OnOffPlugin.onChange(setPluginOnOff);

    // Matter beginning - Last step, after all EndPoints are initialized
    Matter.begin();
    // This may be a restart of a already commissioned Matter accessory
    if (Matter.isDeviceCommissioned()) 
    {
        Serial.println("Matter Node is commissioned and connected to the network. Ready for use");
        Serial.printf("Initial state: %s\r\n", OnOffPlugin.getOnOff() ? "ON" : "OFF");
        OnOffPlugin.updateAccessory();  // configure the Plugin based on initial state
    }
    else
    {
        Serial.printf("Matter comission code is: %s\n", Matter.getManualPairingCode().c_str());
    }

    /** Start scanning for advertisers */ // move this to matter event handler?
    pScan->start(scanTimeMs);
}

void loop()
{
    espConnect.loop();
    
    if (doConnect)
    {
        doConnect = false;
        /** Found a device we want to connect to, do it now */
        // if (connectToSwitchBot())
        if (executeSwitchBotCommand(doCommand))
        {
            Serial.println("Success! we should now be getting notifications");
        }
        else
        {
            Serial.println("Failed to connect");
            OnOffPlugin.setOnOff(false);
            OnOffPlugin.updateAccessory();

            if (auto request = pressRequest.lock()) 
            {
                JsonDocument doc;
                doc["status"] = "ER";
                doc["payload"] = "Error with connection to Switchbot";
                
                String output;
                serializeJson(doc, output);
                request->send(200, "application/json", output);
            }
        }
    }
}