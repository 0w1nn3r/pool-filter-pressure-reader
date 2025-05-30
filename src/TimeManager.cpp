#include "TimeManager.h"

TimeManager::TimeManager() : timeInitialized(false), lastSyncTime(0) {
    ntpClient = new NTPClient(ntpUDP, "pool.ntp.org", 3600); // GMT+1 (3600 seconds offset)
}

TimeManager::~TimeManager() {
    if (ntpClient) {
        delete ntpClient;
    }
}

void TimeManager::begin() {
    ntpClient->begin();
    update(); // Initial time sync
}

void TimeManager::update() {
    unsigned long currentMillis = millis();
    
    // Only sync if not initialized or if sync interval has passed
    if (!timeInitialized || (currentMillis - lastSyncTime >= syncInterval)) {
        if (ntpClient->update()) {
            // Convert NTP time to time_t
            time_t epochTime = ntpClient->getEpochTime();
            setTime(epochTime);
            
            timeInitialized = true;
            lastSyncTime = currentMillis;
            
            Serial.println("Time synchronized with NTP server");
            Serial.print("Current time: ");
            Serial.println(getFormattedDateTime());
        } else {
            Serial.println("Failed to sync time with NTP server");
        }
    }
}

String TimeManager::getFormattedTime() {
    char buffer[9]; // HH:MM:SS + null terminator
    sprintf(buffer, "%02d:%02d:%02d", hour(), minute(), second());
    return String(buffer);
}

String TimeManager::getFormattedDate() {
    char buffer[11]; // YYYY-MM-DD + null terminator
    sprintf(buffer, "%04d-%02d-%02d", year(), month(), day());
    return String(buffer);
}

String TimeManager::getFormattedDateTime() {
    char buffer[20]; // YYYY-MM-DD HH:MM:SS + null terminator
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
            year(), month(), day(), hour(), minute(), second());
    return String(buffer);
}

time_t TimeManager::getCurrentTime() {
    return now();
}
