#include <Arduino.h>
#include <Matter.h>
#include <MycilaESPConnect.h>
#include <MycilaTaskManager.h>
#include <PsychicMqttClient.h>
#include "ReBLEDevice.h"
#include "ReBLEUtils.h"
#include "ReLED.h"
#include "ReServer.h"

static PsychicMqttClient mqttClient;
static ReContext ctx;
static ReBLEDevice bleDevice;
static MatterOnOffPlugin onOffPlugin;

ReServer* server = nullptr;
Mycila::ESPConnect* espConnect = nullptr;

// Task to turn off the switch and update the state after a delay
Mycila::Task offSwitchTask("Turn Off", [](void* params){
    logger.info(RE_TAG, "-> OFF Switch to false");

    if (config.get<bool>("dev_matter"))
    {
        onOffPlugin.setOnOff(false);
        onOffPlugin.updateAccessory();
    }

    if (config.get<bool>("mqtt_en"))
    {
        mqttClient.publish("blegateway/result", 1, true, (char*)params); 
    }

    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_IDLE));
});


// Notification receiving handler callback
void updateAndNotifyWithBleData(std::string& resultData)
{
    server->pressRequestNotifyJson(resultData);

    offSwitchTask.resume(RE_TASK_RESUME_TIME_MS);
    offSwitchTask.setData((void*)resultData.c_str());

    logger.debug(RE_TAG, "Updated accessory with BLE data: %s", resultData.c_str());
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
    if (false == config.get<bool>("mqtt_en"))
    {
        return;
    }

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

    // Load configuration data from NVS
    configureStorage();

    // Setup the Async Web Server and ESPConnect for network management
    // do not change to order of these, as the server needs to be initialized before ESPConnect 
    // can use it for captive portal and config, and ESPConnect needs to be initialized before the 
    // server can use it for network state listening
    server = new ReServer(config.get<int>("dev_port"));

    espConnect = new Mycila::ESPConnect(*server);

    server->setESPConnect(espConnect);
    
    // Network state listener
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

    // Setup and start ESPConnect with the configuration loaded from NVS, or start captive portal if no config or connection fails
    espConnect->setAutoRestart(true);
    espConnect->setConnectTimeout(300);
    espConnect->setBlocking(true);
    logger.debug(RE_TAG, "Trying to connect to saved WiFi or will start portal...");
    espConnect->begin("BLEGateway", "BLEGateway");
    logger.debug(RE_TAG, "ESPConnect completed, continuing setup()...");

    // Start the Async Web Server
    server->begin();
    logger.debug(RE_TAG, "Async Web Server started");

    // Setup the task to turn off the Matter switch and publish MQTT status after a delay, 
    // in case something goes wrong with the BLE connection and it doesn't get turned off properly. 
    // This is a safety mechanism to prevent the switch from being stuck on if there is an issue.
    offSwitchTask.setEnabled(true);
    offSwitchTask.setType(Mycila::Task::Type::ONCE);

    offSwitchTask.onDone([](const Mycila::Task& me, uint32_t elapsed) {
        logger.debug(RE_TAG, "Task '%s' executed in %ld us", me.name(), elapsed);
    });

    // To allow log viewing over the web
    configureWebSerial(config.get<bool>("adm_webserial"), server);

    // Do not move this line to another place, as the BLE device needs to be initialized before Matter 
    bleDevice.initialize(updateAndNotifyWithBleData);

    if (config.get<bool>("dev_matter"))
    {
        logger.debug(RE_TAG, "Initializing Matter On/Off Plugin EndPoint");

        // Start the Matter On/Off Plugin EndPoint and set the user callback for when the state is changed by the Matter Controller
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
    }

    // Do not move this line to another place
    bleDevice.start();

    // If MQTT is enabled in config, setup the MQTT client and connect to the broker
    setupMqttClient();

    // Update the LED to indicate we are ready and waiting for BLE connection and commands
    LED_COLOR_UPDATE(LED_COLOR_GREEN);
    LED_STATUS_UPDATE(start(LED_BLE_SCANNING));
}

void loop()
{
    espConnect->loop();
    
    offSwitchTask.tryRun();
    ReLED.getStatusLED()->check();
    
    // There is a request to connect to the BLE device and execute the command
    if (ctx.getDoConnect())
    {
        ctx.setDoConnect(false);
        
        // Found a device we want to connect to, do it now
        if (bleDevice.executeSwitchBotCommand(ctx.getDoCommand()))
        {
            logger.debug(RE_TAG, "Success! we should now be getting notifications");
            
            LED_COLOR_UPDATE(LED_COLOR_ORANGE);
            LED_STATUS_UPDATE(start(LED_BLE_PROCESSING));
        }
        // If we failed to connect or execute the command, we should reset the state and notify the user
        else
        {
            logger.error(RE_TAG, "Failed to connect");

            LED_COLOR_UPDATE(LED_COLOR_RED);
            LED_STATUS_UPDATE(start(LED_BLE_ALERT));

            offSwitchTask.resume(RE_TASK_RESUME_TIME_MS);

            std::string resultData = "ERError with connection to Switchbot";
            server->pressRequestNotifyJson(resultData);
        }
    }
}