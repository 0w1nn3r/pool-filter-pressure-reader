#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include "TimeManager.h"
#include "BackflushLogger.h"
#include "Settings.h"
#include "PressureLogger.h"

class WebServer {
private:
    ESP8266WebServer server;
    float& currentPressure;
    float& backflushThreshold;
    unsigned int& backflushDuration;
    bool& backflushActive;
    unsigned long& backflushStartTime;
    bool& backflushConfigChanged;
    TimeManager& timeManager;
    BackflushLogger& backflushLogger;
    Settings& settings;
    PressureLogger& pressureLogger;

    void handleRoot();
    void handleAPI();
    void handleBackflushConfig();
    void handleBackflushLog();
    void handleClearLog();
    void handlePressureHistory();
    void handleClearPressureHistory();

public:
    WebServer(float& pressure, float& threshold, unsigned int& duration, 
              bool& active, unsigned long& startTime, bool& configChanged,
              TimeManager& tm, BackflushLogger& logger, Settings& settings,
              PressureLogger& pressureLog);
    void begin();
    void handleClient();
};

#endif // WEBSERVER_H
