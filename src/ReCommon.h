#pragma once

#include <MycilaConfig.h>
#include <MycilaLogger.h>
#include <MycilaConfigStorageNVS.h>
#include <MycilaWebSerial.h>
#include <ESPAsyncWebServer.h>

#define RELINE(line) #line
#define RETAG(a, b) a ":" RELINE(b)
#define RE_TAG RETAG(__FILE__, __LINE__)

#define RE_TASK_RESUME_TIME_MS 5000

extern const uint8_t settings_html_start[] asm("_binary__pio_embed_settings_html_gz_start");
extern const uint8_t settings_html_end[] asm("_binary__pio_embed_settings_html_gz_end");

inline Mycila::config::NVS storage;
inline Mycila::config::Config config(storage);

inline Mycila::Logger logger;
inline WebSerial* webSerial = nullptr;

void configureStorage()
{
   // Declare configuration keys with optional default values
   // Key names must be ≤ 15 characters
   config.configure("net_ssid", "");
   config.configure("net_pass", "");
   config.configure("dev_port", 80);
   config.configure("mqtt_en", false);
   config.configure("mqtt_ip", "");
   config.configure("mqtt_port", 1883);
   config.configure("mqtt_user", "");
   config.configure("mqtt_pass", "");
   config.configure("bot_mac", "f2:b2:02:06:1d:21");
   config.configure("bot_scantime", 5000);
   config.configure("bot_txpower", 11);
   config.configure("adm_pass", "admin");
   config.configure("adm_webserial", false);

   config.begin("BLEGateway", true); // Preload all values
}

void configureWebSerial(bool enabled, const AsyncWebServer* server)
{
   if (enabled)
   {
      if (nullptr != server)
      {
         webSerial = new WebSerial();
         webSerial->setBuffer(128);
         webSerial->begin(const_cast<AsyncWebServer*> (server));
         logger.forwardTo(webSerial);

         logger.debug(RE_TAG, "Using WebSerial logger");
      }
      else
      {
         logger.error(RE_TAG, "AsyncWebServer not initialized");
      }
   }
}