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

time_t TimeManager::getCurrentTime() const {
    return now();
}

String TimeManager::getCurrentTimeStr() const {
    return formatTime(getCurrentTime());
}

String TimeManager::formatTime(time_t t) const {
    char buffer[9]; // HH:MM:SS + null terminator
    struct tm* timeinfo = localtime(&t);
    sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buffer);
}

String TimeManager::formatDate(time_t t) const {
    char buffer[11]; // YYYY-MM-DD + null terminator
    struct tm* timeinfo = localtime(&t);
    sprintf(buffer, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
    return String(buffer);
}

String TimeManager::getFormattedDateTime() const {
    char buffer[20]; // YYYY-MM-DD HH:MM:SS + null terminator
    time_t t = getCurrentTime();
    struct tm* timeinfo = localtime(&t);
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
            timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return String(buffer);
}
