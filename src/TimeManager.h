#ifndef TIMEMANAGER_H
#define TIMEMANAGER_H

#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

class TimeManager {
private:
    WiFiUDP ntpUDP;
    NTPClient* ntpClient;
    bool timeInitialized;
    unsigned long lastSyncTime;
    const unsigned long syncInterval = 3600000; // Sync every hour

public:
    TimeManager();
    ~TimeManager();
    
    void begin();
    void update();
    bool isTimeInitialized() const { return timeInitialized; }
    time_t getCurrentTime() const;
    String getCurrentTimeStr() const;
    String formatTime(time_t t) const;
    String formatDate(time_t t) const;
    String getFormattedDateTime() const;
};

#endif // TIMEMANAGER_H
