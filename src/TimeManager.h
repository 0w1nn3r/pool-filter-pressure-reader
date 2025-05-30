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
    
    String getFormattedTime(); // Returns HH:MM:SS
    String getFormattedDate(); // Returns YYYY-MM-DD
    String getFormattedDateTime(); // Returns YYYY-MM-DD HH:MM:SS
    
    time_t getCurrentTime(); // Returns Unix timestamp
};

#endif // TIMEMANAGER_H
