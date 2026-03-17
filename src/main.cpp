#include <Arduino.h>
#include <Matter.h>
#include <NimBLEDevice.h>
#include <MycilaESPConnect.h>
#include <MycilaSystem.h>
#include <MycilaTaskManager.h>
#include <MycilaWebSerial.h>
#include <PsychicMqttClient.h>
#include "ReCommon.h"
#include "ReBLEUtils.h"
#include "ReBLEDevice.h"
#include "ReLED.h"
#include "ReContext.h"
#include "ReServer.h"

static PsychicMqttClient mqttClient;
static ReContext ctx;

ReServer* server = nullptr;
Mycila::ESPConnect* espConnect = nullptr;

// static const NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEScan* pScan = nullptr;

ReClientCallbacks clientCallbacks;
ReScanCallbacks scanCallbacks;

static MatterOnOffPlugin onOffPlugin;

static BLEUUID serviceUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID controlCharacteristicUUID("cba20002-224d-11e6-9fb8-0002a5d5c51b");
static BLEUUID notifyCharacteristicUUID("cba20003-224d-11e6-9fb8-0002a5d5c51b");

Mycila::Task offMatterSwitchTask("Turn Off", [](void* params){
    logger.info(RE_TAG, "-> OFF Switch to false");

    onOffPlugin.setOnOff(false);
    onOffPlugin.updateAccessory();

    if (config.get<bool>("mqtt_en"))
    {
        mqttClient.publish("blegateway/result", 1, true, "OK"); 
    }

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_IDLE));
});

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

    server->pressRequestNotifyJson(resultData);

    offMatterSwitchTask.resume(RE_TASK_RESUME_TIME_MS);
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

// Matter & MQTT protocol Endpoint Callback
bool setPluginOnOff(bool state) {
    logger.info(RE_TAG, "User Callback :: New Plugin State = %s", state ? "ON" : "OFF");
  
    ctx.setDoCommand(BOT_PRESS_COMMAND);

    if (state && ctx.getBleDeviceFound())
    {
        ctx.setDoConnect(true);
    }

    return true;
}

void setupMqttClient()
{
    std::string mqttIp = config.getString("mqtt_ip");
    std::string mqttServer = "mqtt://" + mqttIp + ":" + std::to_string(config.get<int>("mqtt_port"));
    mqttClient.setServer(mqttServer.c_str());
    mqttClient.setCredentials(config.getString("mqtt_user"), config.getString("mqtt_pass"));
    mqttClient.setClientId("BLEGateway");
    mqttClient.setCleanSession(false);
    mqttClient.setKeepAlive(60);
    mqttClient.setWill("blegateway/status", 1, true, "BLEGateway OFFLINE");

    mqttClient.onTopic("blegateway/control", 2, [&](const char *topic, const char *payload, int retain, int qos, bool dup)
        {
            logger.debug(RE_TAG, "Received Topic: %s", topic);
            logger.debug(RE_TAG, "Received Payload: %s", payload);

            if (!strcmp(payload, BOT_PRESS_COMMAND))
            {
                setPluginOnOff(true);
            }
        });

    mqttClient.onConnect([&](bool sessionPresent)
        {
            logger.debug(RE_TAG, "MQTT connected: %s, sessionPresent: %d", mqttClient.connected() ? "YES" : "NO", sessionPresent);
            logger.debug(RE_TAG, "MQTT clientID: %s", mqttClient.getClientId());
            
            mqttClient.publish("blegateway/status", 1, true, "BLEGateway: ONLINE"); 
        });

    mqttClient.connect();
}

void setup()
{
    Serial.begin(115200);

    logger.forwardTo(&Serial);
    logger.debug(RE_TAG, "Using Serial as terminal");
    // homeSpan.setControlPin(41, PushButton::TRIGGER_ON_LOW);

    // Configure RGB led or normal led, depending on the board type
#ifdef PIN_RGB_LED
    ReLED.begin(PIN_RGB_LED, true, 300);
#else

#ifdef USB_PRODUCT 
    if (String(USB_PRODUCT) == "NanoC6")
    {
        pinMode(RGB_LED_PWR_PIN, OUTPUT);
        digitalWrite(RGB_LED_PWR_PIN, HIGH);
        ReLED.begin(RGB_LED_DATA_PIN, true, 300);
    }
    else
    {
        ReLED.begin(LED_BUILTIN, false, 300);
    }
#else
    ReLED.begin(LED_BUILTIN, false, 300);
#endif

#endif    
    
    LED_STATUS_UPDATE(start(LED_WIFI_NEEDED));

    logger.debug(RE_TAG, "Starting BLE and Matter ESP32 Gateway to Switchbot Bot");

    // load configuration data from NVS
    configureStorage();

    // setup the Async Web Server and ESPConnect for network management
    // do not change to order of these, as the server needs to be initialized before ESPConnect 
    // can use it for captive portal and config, and ESPConnect needs to be initialized before the 
    // server can use it for network state listening
    server = new ReServer(config.get<int>("dev_port"));

    espConnect = new Mycila::ESPConnect(*server);

    server->setESPConnect(espConnect);
    
    // network state listener
    espConnect->listen([&](__unused Mycila::ESPConnect::State previous, __unused Mycila::ESPConnect::State state) 
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
        espConnect->toJson(doc.to<JsonObject>());
        serializeJsonPretty(doc, Serial);
    });

    espConnect->setAutoRestart(true);
    espConnect->setConnectTimeout(300);
    espConnect->setBlocking(true);
    logger.debug(RE_TAG, "Trying to connect to saved WiFi or will start portal...");
    espConnect->begin("BLEGateway", "BLEGateway");
    logger.debug(RE_TAG, "ESPConnect completed, continuing setup()...");

    server->begin();

    logger.debug(RE_TAG, "Async Web Server started");

    // setup the task to turn off the Matter switch after a delay, 
    // in case something goes wrong with the BLE connection and it doesn't get turned off properly. 
    // This is a safety mechanism to prevent the switch from being stuck on if there is an issue.
    offMatterSwitchTask.setEnabled(true);
    offMatterSwitchTask.setType(Mycila::Task::Type::ONCE);

    offMatterSwitchTask.onDone([](const Mycila::Task& me, uint32_t elapsed) {
        logger.debug(RE_TAG, "Task '%s' executed in %ld us", me.name(), elapsed);
    });

    // To allow log viewing over the web
    configureWebSerial(config.get<bool>("adm_webserial"), server);

    /** Initialize NimBLE and set the device name */
    NimBLEDevice::init("SwitchBot-Bot-Client");
    NimBLEDevice::whiteListAdd(NimBLEAddress(config.getString("bot_mac"), 0));
    NimBLEDevice::setPower((esp_power_level_t) config.get<int>("bot_txpower"));
    
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

    onOffPlugin.begin();
    onOffPlugin.onChange(setPluginOnOff);

    // Matter beginning - Last step, after all EndPoints are initialized
    Matter.begin();

    // This may be a restart of a already commissioned Matter accessory
    if (Matter.isDeviceCommissioned()) 
    {
        logger.debug(RE_TAG, "Matter Node is commissioned and connected to the network. Ready for use");
        logger.debug(RE_TAG, "Initial state: %s", onOffPlugin.getOnOff() ? "ON" : "OFF");
        onOffPlugin.updateAccessory();  // configure the Plugin based on initial state
    }
    else
    {
        logger.debug(RE_TAG, "Matter comission code is: %s", Matter.getManualPairingCode().c_str());
    }

    /** Start scanning for advertisers */ // move this to matter event handler?
    int scanTimeMs = config.get<int>("bot_scantime");
    pScan->start(scanTimeMs);

    if (config.get<bool>("mqtt_en"))
    {
        setupMqttClient();
    }

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
}

void loop()
{
    espConnect->loop();
    
    offMatterSwitchTask.tryRun();
    ReLED.getStatusLED()->check();
    
    if (ctx.getDoConnect())
    {
        ctx.setDoConnect(false);
        
        /** Found a device we want to connect to, do it now */
        if (executeSwitchBotCommand(ctx.getDoCommand()))
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

            offMatterSwitchTask.resume(RE_TASK_RESUME_TIME_MS);

            std::string resultData = "ERError with connection to Switchbot";
            server->pressRequestNotifyJson(resultData);
        }
    }
}