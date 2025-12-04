#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Matter.h>
#include <ESPAsyncWebServer.h>
#include <MycilaESPConnect.h>
#include <MycilaSystem.h>
#include <MycilaTaskManager.h>
#include <MycilaWebSerial.h>
#include "ReCommon.h"
#include "ReBLEUtils.h"
#include "ReBLEConfig.h"
#include "ReLED.h"

static AsyncWebServer server(webServerPort);
static AsyncWebServerRequestPtr pressRequest;
// basicAuth
static AsyncAuthenticationMiddleware basicAuth;

static Mycila::ESPConnect espConnect(server);

static const NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEScan* pScan = nullptr;

// these can be inline global variables
inline bool doConnect = false;
inline std::string doCommand = "570100"; // default is press command
inline MatterOnOffPlugin OnOffPlugin;

static BLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID controlCharacteristicUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID notifyCharacteristicUUID("cba20003-224d-11e6-9fb8-0002a5d5c51b");

Mycila::Task offMatterSwitchTask("Turn Off", [](void* params){
    logger.info(RE_TAG, "-> OFF Switch to false");

    OnOffPlugin.setOnOff(false);
    OnOffPlugin.updateAccessory();

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_IDLE));
});

class ClientCallbacks : public NimBLEClientCallbacks
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

} clientCallbacks;

/** Define a class to handle the callbacks when scan events are received */
class ScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override
    {
        // This is a device with our MAC address
        std::string botMacAddr = config.getString("ble_mac");
        if (advertisedDevice->getAddress().toString() == botMacAddr)
        {
            logger.info(RE_TAG, "Advertised Device found: %s", advertisedDevice->getAddress().toString().c_str());

            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();

            /** Save the device reference in a global for the client to use*/
            advDevice = advertisedDevice;

            LED_COLOR_UPDATE(LED_COLOR_GREEN);
            LED_STATUS_UPDATE(on());
            
            // doConnect = true;
        }
    }

    /** Callback to process the results of the completed scan or restart it */
    void onScanEnd(const NimBLEScanResults &results, int reason) override
    {
        logger.info(RE_TAG, "Scan Ended, reason: %d, device count: %d; Restarting scan\n", reason, results.getCount());
        int scanTimeMs = config.get<int>("scan_time");
        NimBLEDevice::getScan()->start(scanTimeMs, false, true);

        LED_COLOR_UPDATE(LED_COLOR_GREEN);
        LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
    }
} scanCallbacks;

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
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

    if (auto request = pressRequest.lock()) 
    {
        JsonDocument doc;
        doc["status"] = resultData.substr(0, 2);
        doc["payload"] = resultData.substr(2);
        
        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    } 

    offMatterSwitchTask.resume(10000);
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
                logger.info(RE_TAG, "Client exists and is already conected");
                return true;
            } 
            else
            {
                if (!pClient->connect(advDevice, false))
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

        if (!pClient->connect(advDevice))
        {
            /** Created a client but failed to connect, don't need to keep it as it has no data */
            NimBLEDevice::deleteClient(pClient);

            logger.error(RE_TAG, "Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected())
    {
        if (!pClient->connect(advDevice))
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
                if (!pChr->subscribe(true, notifyCB))
                {
                    pClient->disconnect();
                    return false;
                }

                logger.info(RE_TAG, "Notifications have been set");
            }
            else if (pChr->canIndicate())
            {
                /** Send false as first argument to subscribe to indications instead of notifications */
                if (!pChr->subscribe(false, notifyCB))
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

bool executeSwitchBotCommand(std::string cmd)
{
    // Establish connection with the client
    if (!connectToSwitchBot())
    {
        logger.error(RE_TAG, "executeSwitchBotCommand: Client not created");
        return false;
    }

    NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    
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

// Matter Protocol Endpoint Callback
bool setPluginOnOff(bool state) {
    logger.info(RE_TAG, "User Callback :: New Plugin State = %s", state ? "ON" : "OFF");
  
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

    logger.forwardTo(&Serial);
    logger.debug(RE_TAG, "Using Serial as terminal");
    // homeSpan.setControlPin(41, PushButton::TRIGGER_ON_LOW);

    // Configure RGB led or normal led, depending on the board type
#ifdef PIN_RGB_LED
    ReLED.begin(PIN_RGB_LED, true, 0);
#else

#ifdef USB_PRODUCT && USB_PRODUCT == "NanoC6"
    pinMode(RGB_LED_PWR_PIN, OUTPUT);
    digitalWrite(RGB_LED_PWR_PIN, HIGH);
    ReLED.begin(RGB_LED_DATA_PIN, true, 0);
#else
    ReLED.begin(LED_BUILTIN, false, 0);
#endif

#endif    
    
    LED_STATUS_UPDATE(start(LED_WIFI_NEEDED));

    logger.debug(RE_TAG, "Starting BLE and Matter ESP32 Gateway to Switchbot Bot");

    // load configuration data from NVS
    configureStorage();

    // basic authentication
    basicAuth.setUsername("admin");
    basicAuth.setPassword("admin");
    basicAuth.setRealm("MyApp");
    basicAuth.setAuthFailureMessage("Authentication failed");
    basicAuth.setAuthType(AsyncAuthType::AUTH_BASIC);
    basicAuth.generateHash();  // precompute hash (optional but recommended)

    offMatterSwitchTask.setEnabled(true);
    offMatterSwitchTask.setType(Mycila::Task::Type::ONCE);

    offMatterSwitchTask.onDone([](const Mycila::Task& me, uint32_t elapsed) {
        logger.debug(RE_TAG, "Task '%s' executed in %ld us", me.name(), elapsed);
    });

    // network state listener
    espConnect.listen([](__unused Mycila::ESPConnect::State previous, __unused Mycila::ESPConnect::State state) 
    {
        switch (state)
        {
            case Mycila::ESPConnect::State::NETWORK_CONNECTING:
            case Mycila::ESPConnect::State::NETWORK_RECONNECTING:
                LED_STATUS_UPDATE(start(LED_WIFI_CONNECTING));
                break;
            case Mycila::ESPConnect::State::NETWORK_DISCONNECTED:
            case Mycila::ESPConnect::State::NETWORK_TIMEOUT:
                LED_STATUS_UPDATE(start(LED_ALERT));
                break;
            case Mycila::ESPConnect::State::NETWORK_CONNECTED:
                LED_STATUS_UPDATE(on());
                break;
            default:
                break;
        }

        JsonDocument doc;
        espConnect.toJson(doc.to<JsonObject>());
        serializeJsonPretty(doc, Serial);
        Serial.println();
    });

    espConnect.setAutoRestart(true);
    espConnect.setBlocking(true);
    logger.debug(RE_TAG, "Trying to connect to saved WiFi or will start portal...");
    espConnect.begin("BLEGateway", "BLEGateway");
    logger.debug(RE_TAG, "ESPConnect completed, continuing setup()...");

    // serve your home page here
    server.on("/", handleRoot).setFilter([](__unused AsyncWebServerRequest* request) 
    { 
        return espConnect.getState() != Mycila::ESPConnect::State::PORTAL_STARTED; 
    });
    
    // clear persisted config
    server.on("/admin/clear", HTTP_GET, [&](AsyncWebServerRequest* request) 
    {
        espConnect.clearConfiguration();
        request->send(200);
        ESP.restart();
    }).addMiddleware(&basicAuth);

    server.on(AsyncURIMatcher::exact("/admin"), HTTP_GET, [&](AsyncWebServerRequest* request) 
    {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)(update_html_start), update_html_end - update_html_start);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
        // server.send_P(200, "text/html", reinterpret_cast<const char*>(update_html_start), update_html_end - update_html_start);
    });

    // get stored admin settings
    server.on(AsyncURIMatcher::exact("/admin/settings"), HTTP_GET, [&](AsyncWebServerRequest* request) 
    {
        AsyncJsonResponse *response = new AsyncJsonResponse();
        JsonObject doc = response->getRoot().to<JsonObject>();
        doc["ble_mac"] = config.getString("ble_mac");
        doc["web_port"] = config.get<int>("web_port");
        doc["scan_time"] = config.get<int>("scan_time");
        doc["ble_power"] = config.get<int>("ble_power");
        doc["admin_pass"] = config.getString("admin_pass");
        doc["webserial_on"] = config.get<bool>("webserial_on");
        
        response->setLength();
        request->send(response);
    });

     // store new admin settings
    server.on(AsyncURIMatcher::exact("/admin/settings"), HTTP_POST, [&](AsyncWebServerRequest* request, JsonVariant &json) 
    {
        JsonObject doc = json.as<JsonObject>();

        config.setString("ble_mac", doc["ble_mac"].as<const char*>());
        config.set<int>("web_port", doc["web_port"].as<int>());
        config.set<int>("scan_time", doc["scan_time"].as<int>());
        config.set<int>("ble_power", doc["ble_power"].as<int>());
        config.setString("admin_pass", doc["admin_pass"].as<const char*>());
        config.set<bool>("webserial_on", doc["webserial_on"].as<bool>());

        serializeJson(doc, Serial);

        request->send(200, "application/json", "{}");
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
    }).addMiddleware(&basicAuth);

    server.on("/admin/safeboot", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/html", "<form method='POST' action='/admin/safeboot' enctype='multipart/form-data'><input type='submit' value='Restart in SafeBoot mode'></form>");
    }).addMiddleware(&basicAuth);

    server.on("/admin/safeboot", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        request->send(200, "text/plain", "Restarting in SafeBoot mode...");
        Mycila::System::restartFactory("safeboot", 1000);
    }).addMiddleware(&basicAuth);

    server.on("/admin/decomission", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        logger.debug(RE_TAG, "Decommissioning the Plugin Matter Accessory. It shall be commissioned again");
        OnOffPlugin.setOnOff(false);
        Matter.decommission();

        request->send(200, "text/plain", "Decommissioning the Matter Accessory. It shall be commissioned again");
    }).addMiddleware(&basicAuth);;

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

    // To allow log viewing over the web
    configureWebSerial(false, &server);

    logger.debug(RE_TAG, "Async Web Server started");

    /** Initialize NimBLE and set the device name */
    NimBLEDevice::init("SwitchBot-Bot-Client");
    NimBLEDevice::whiteListAdd(NimBLEAddress(config.getString("ble_mac"), 0));
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
        logger.debug(RE_TAG, "Matter Node is commissioned and connected to the network. Ready for use");
        logger.debug(RE_TAG, "Initial state: %s", OnOffPlugin.getOnOff() ? "ON" : "OFF");
        OnOffPlugin.updateAccessory();  // configure the Plugin based on initial state
    }
    else
    {
        logger.debug(RE_TAG, "Matter comission code is: %s", Matter.getManualPairingCode().c_str());
    }

    /** Start scanning for advertisers */ // move this to matter event handler?
    int scanTimeMs = config.get<int>("scan_time");
    pScan->start(scanTimeMs);

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
}

void loop()
{
    espConnect.loop();
    offMatterSwitchTask.tryRun();
    
    if (doConnect)
    {
        doConnect = false;
        /** Found a device we want to connect to, do it now */
        if (executeSwitchBotCommand(doCommand))
        {
            logger.debug(RE_TAG, "Success! we should now be getting notifications");
            
            LED_COLOR_UPDATE(LED_COLOR_ORANGE);
            LED_STATUS_UPDATE(start(LED_BLE_PROCESSING));
        }
        else
        {
            logger.error(RE_TAG, "Failed to connect");

            LED_COLOR_UPDATE(LED_COLOR_RED);
            LED_STATUS_UPDATE(start(LED_BLE_ALERT));

            offMatterSwitchTask.resume(5000);

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