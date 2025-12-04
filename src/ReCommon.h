#pragma once

#include <MycilaConfig.h>
#include <MycilaLogger.h>
#include <MycilaConfigStorageNVS.h>
#include <MycilaWebSerial.h>
#include <ESPAsyncWebServer.h>

#define RELINE(line) #line
#define RETAG(a, b) a ":" RELINE(b)
#define RE_TAG RETAG(__FILE__, __LINE__)

extern const uint8_t update_html_start[] asm("_binary__pio_embed_settings_html_gz_start");
extern const uint8_t update_html_end[] asm("_binary__pio_embed_settings_html_gz_end");

inline Mycila::config::NVS storage;
inline Mycila::config::Config config(storage);

inline Mycila::Logger logger;
inline WebSerial* webSerial = nullptr;

void configureStorage()
{
   // Declare configuration keys with optional default values
   // Key names must be ≤ 15 characters
   config.configure("webserial_on", false);
   config.configure("ble_mac", "d8:bf:c4:c6:51:0c");
   config.configure("web_port", 80);
   config.configure("scan_time", 5000);
   config.configure("ble_power", 11);
   config.configure("admin_pass", "admin");

   config.begin("BLEGateway", true); // Preload all values
}

void configureWebSerial(bool enabled, AsyncWebServer* server)
{
   if (enabled)
   {
      if (nullptr != server)
      {
         webSerial = new WebSerial();
         webSerial->setBuffer(128);
         webSerial->begin(server);
         logger.forwardTo(webSerial);

         logger.debug(RE_TAG, "Using WebSerial logger");
      }
      else
      {
         logger.error(RE_TAG, "AsyncWebServer not initialized");
      }
   }
}