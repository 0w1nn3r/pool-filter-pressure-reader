#ifndef TIMEMANAGER_H
#define TIMEMANAGER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

class TimeManager {
private:
    WiFiUDP ntpUDP;
    NTPClient* ntpClient;
    bool timeInitialized;
    unsigned long lastSyncTime;
    const unsigned long syncInterval = 3600000; // Sync every hour
    int32_t timezoneOffset = 0; // Timezone offset in seconds
    bool timezoneInitialized = false;
    
    String getPublicIP();
    bool detectTimezone(const String& ip);

public:
    TimeManager();
    ~TimeManager();
    
    void begin();
    void update();
    bool isTimeInitialized() const { return timeInitialized; }
    // GMT/UTC time methods (for storage)
    time_t getCurrentGMTTime() const;
    String formatGMTTime(time_t t) const;
    String formatGMTDate(time_t t) const;
    String getFormattedGMTDateTime() const;
    
    // Local time methods (for display)
    time_t getCurrentTime() const;
    String getCurrentTimeStr() const;
    String formatTime(time_t t) const;
    String formatDate(time_t t) const;
    String getFormattedDateTime() const;
    
    // Convert between GMT and local time
    time_t gmtToLocal(time_t gmtTime) const;
    time_t localToGMT(time_t localTime) const;
    int32_t getTimezoneOffset() const { return timezoneOffset; }
    bool isTimezoneInitialized() const { return timezoneInitialized; }
};

#endif // TIMEMANAGER_H
