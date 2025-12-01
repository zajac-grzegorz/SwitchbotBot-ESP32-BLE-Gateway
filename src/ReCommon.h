#pragma once

#include <MycilaConfig.h>
#include <MycilaLogger.h>
#include <MycilaWebSerial.h>
#include <ESPAsyncWebServer.h>

#define RELINE(line) #line
#define RETAG(a, b) a ":" RELINE(b)
#define RE_TAG RETAG(__FILE__, __LINE__)

inline Mycila::Logger logger;
inline WebSerial webSerial;

void configureWebSerial(bool enabled, AsyncWebServer* server)
{
   if (enabled)
   {
      if (nullptr != server)
      {
         logger.forwardTo(&webSerial);
         webSerial.setBuffer(256);
         webSerial.begin(server);

         logger.debug(RE_TAG, "Using WebSerial logger");
      }
      else
      {
         logger.error(RE_TAG, "AsyncWebServer not initialized");
      }
   }
}