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

void configureStorage();
void configureWebSerial(bool enabled, const AsyncWebServer* server);