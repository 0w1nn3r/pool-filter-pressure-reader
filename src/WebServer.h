#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "TimeManager.h"
#include "BackflushLogger.h"
#include "Settings.h"
#include "PressureLogger.h"
#include "BackflushScheduler.h"
#include "Display.h"

// External pin definitions from main.cpp
extern const int RELAY_PIN;
extern const int LED_PIN;

// External pressure sensor constants from main.cpp
extern float PRESSURE_MAX;
extern const float PRESSURE_MIN;
extern float VOLTAGE_MIN;
extern float VOLTAGE_MAX;

class WebServer {
private:
    ESP8266WebServer server;
    float& currentPressure;
    int& rawADCValue;         // Reference to raw ADC value
    float& sensorVoltage;     // Reference to sensor voltage
    float& backflushThreshold;
    unsigned int& backflushDuration;
    bool& backflushActive;
    unsigned long& backflushStartTime;
    bool& backflushConfigChanged;
    String& currentBackflushType;
    TimeManager& timeManager;
    BackflushLogger& backflushLogger;
    Settings& settings;
    BackflushScheduler& scheduler;
    
    // OTA update variables
    unsigned long otaEnabledTime;
    bool otaEnabled = false;
    static const unsigned long OTA_TIMEOUT = 300000; // 5 minutes in milliseconds
    
    PressureLogger& pressureLogger;
 
    // Display reference for OTA updates
    Display* display;

    // Helper function to draw arc segments for the gauge
    String drawArcSegment(float cx, float cy, float radius, float startAngle, float endAngle, String color, float opacity);
    
    void handleRoot();
    void handleAPI();
    void handleCSS();
    void handleBackflushConfig();
    void handleBackflushLog();
    void handleClearLog();
    void handlePressureHistory();
    void handleClearPressureHistory();
    void handleWiFiConfigPage();
    void handleManualBackflush();
    void handleStopBackflush();
    void handleSettings();
    void handleSensorConfig();
    void handleSetRetention();
    void handlePressureCsv();
    void handleOTAUpdate();
    void setupOTA();
    void handleOTAUploadPage();
    void handleOTAUpload();
    void handleSchedulePage();
    void handleScheduleUpdate();
    void handleScheduleDelete();

public:
    WebServer(float& pressure, int& rawADC, float& voltage, float& threshold, unsigned int& duration, 
             bool& active, unsigned long& startTime, bool& configChanged,
             String& backflushType, TimeManager& tm, BackflushLogger& logger, 
             Settings& settings, PressureLogger& pressureLog, BackflushScheduler& sched);
    
    void setDisplay(Display* displayPtr) { display = displayPtr; }
    void begin();
    void handleClient();
    bool isOTAEnabled() const { return otaEnabled; }
};

#endif // WEBSERVER_H
