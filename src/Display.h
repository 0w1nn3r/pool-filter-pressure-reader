#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>

class Display {
private:
    Adafruit_SSD1306& display;
    float& currentPressure;
    float& backflushThreshold;
    unsigned int& backflushDuration;
    bool& backflushActive;
    unsigned long& backflushStartTime;
    bool displayAvailable;

public:
    Display(Adafruit_SSD1306& oled, float& pressure, float& threshold, 
            unsigned int& duration, bool& active, unsigned long& startTime);
    bool init();
    void showStartupScreen();
    void showWiFiConnecting();
    void showWiFiConnected(String ssid, IPAddress ip);
    void showWiFiSetupMode(String apName);
    void showResetMessage();
    void updateDisplay();
    bool isDisplayAvailable() const;
};

#endif // DISPLAY_H
