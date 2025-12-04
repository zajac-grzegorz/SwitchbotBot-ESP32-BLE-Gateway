#pragma once

#include <Arduino.h>
#include "extras/Blinker.h"
#include "extras/Pixel.h"

#define LED_WIFI_NEEDED         300,0.5,1,2700      // slow single-blink
#define LED_PAIRING_NEEDED      300,0.5,2,2400      // slow double-blink
#define LED_ALERT               100                 // rapid flashing
#define LED_WIFI_CONNECTING     2000                // slow flashing
#define LED_WIFI_SCANNING       300,0.8,3,400       // medium inverted triple-blink
#define LED_BLE_SCANNING        1000                // medium flashing
#define LED_BLE_ALERT           500                 
#define LED_BLE_PROCESSING      500,0.5,2,200
#define LED_BLE_IDLE            5000,0.5,2,0

#define LED_COLOR_RED           100,0,0
#define LED_COLOR_GREEN         0,100,0
#define LED_COLOR_BLUE          0,0,100
#define LED_COLOR_ORANGE        255,190,0

#define LED_STATUS_UPDATE(LED_UPDATE) {ReLED.getStatusLED()->LED_UPDATE;}
#define LED_COLOR_UPDATE(RGB_COLOR) { \
    if (ReLED.isRGB()) \
    { \
        ((Pixel*) ReLED.getStatusDevice())->setOnColor(Pixel::RGB(RGB_COLOR)); \
        ReLED.getStatusLED()->refresh(); \
    } \
}

class ReLEDClass
{
public:
    void begin(uint8_t pin, bool isRGB = false, uint16_t autoOffDuration = 0) 
    {
        ledPin = pin;
        isRGBLED = isRGB;

        if (isRGB)
        {
            setStatusPixel(ledPin, 0, 0, 100);
        }
        else
        {
            setStatusPin(ledPin);
        }

        // create Status LED, even is statusDevice is NULL
        statusLED = new Blinker(statusDevice, autoOffDuration);
    }

    Blinker* getStatusLED() { return statusLED; }
    Blinkable* getStatusDevice() { return statusDevice; }
    bool isRGB() { return isRGBLED; }
    uint8_t getPin() { return ledPin; }

private:

    // sets Status Device to a simple LED on specified pin
    void setStatusPin(uint8_t pin)
    {
        statusDevice = new GenericLED(pin);
        // statusDevice->isRGB = false;
    }

    // sets Status Device to an RGB Pixel on specified pin
    void setStatusPixel(uint8_t pin, uint8_t r=0, uint8_t g=0, uint8_t b=100)
    {
        statusDevice = ((new Pixel(pin))->setOnColor(Pixel::RGB(r,g,b)));
        // statusDevice->isRGB = true;
    }

    Blinker *statusLED = nullptr;           // indicates status
    Blinkable *statusDevice = nullptr;      // the device used for the Blinker
    bool isRGBLED = false;
    uint8_t ledPin = LED_BUILTIN;
} ReLED;
