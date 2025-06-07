#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include "TimeManager.h"

// Forward declarations
class WebServer;
class BackflushScheduler;

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
    WebServer* webServer;
    BackflushScheduler* scheduler;
    unsigned long lastOtaFlashTime;
    bool showOtaText;

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
    void setWebServer(WebServer* ws) { webServer = ws; }
    void setScheduler(BackflushScheduler* sched) { scheduler = sched; }
    void showResetMessage();
    void updateDisplay();
    void showFirmwareUpdateProgress(int percentage);
    bool isDisplayAvailable() const;
    void showResetCountdown(String message, unsigned int countdownSeconds);
    
    // Show a message on the display with a title and message
    void showMessage(const String& title, const String& message);
    
    // Show the next scheduled backflush time
    void showNextScheduledBackflush(time_t nextTime, unsigned int duration);
};

#endif // DISPLAY_H
