#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "TimeManager.h"
#include "BackflushLogger.h"
#include "Settings.h"
#include "PressureLogger.h"

// External pin definitions from main.cpp
extern const int RELAY_PIN;
extern const int LED_PIN;

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

    // Helper function to draw arc segments for the gauge
    String drawArcSegment(float cx, float cy, float radius, float startAngle, float endAngle, String color, float opacity);
    
    void handleRoot();
    void handleAPI();
    void handleBackflushConfig();
    void handleBackflushLog();
    void handleClearLog();
    void handlePressureHistory();
    void handleClearPressureHistory();
    void handleWiFiReset();
    void handleManualBackflush();
    void handleStopBackflush();
    void handleSettings();
    void handleSensorConfig();
    void handleSetRetention();
    void handlePressureCsv();

public:
    WebServer(float& pressure, float& threshold, unsigned int& duration, 
              bool& active, unsigned long& startTime, bool& configChanged,
              TimeManager& tm, BackflushLogger& logger, Settings& settings,
              PressureLogger& pressureLog);
    void begin();
    void handleClient();
};

#endif // WEBSERVER_H
