#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include "TimeManager.h"

class Display {
private:
    Adafruit_SSD1306& display;
    float& currentPressure;
    float& backflushThreshold;
    unsigned int& backflushDuration;
    bool& backflushActive;
    unsigned long& backflushStartTime;
    bool displayAvailable;
    TimeManager* timeManager;

public:
    Display(Adafruit_SSD1306& oled, float& pressure, float& threshold, 
            unsigned int& duration, bool& active, unsigned long& startTime, TimeManager* tm);
    bool init();
    void showStartupScreen();
    void showWiFiConnecting();
    void showWiFiConnected(String ssid, IPAddress ip);
    void showTimezone();
    void showWiFiSetupMode(String apName);
    void setTimeManager(TimeManager* tm) { timeManager = tm; }
    void showResetMessage();
    void updateDisplay();
    bool isDisplayAvailable() const;
};

#endif // DISPLAY_H
