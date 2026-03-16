#include "ReCommon.h"

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