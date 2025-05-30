#ifndef PRESSURELOGGER_H
#define PRESSURELOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "TimeManager.h"

// Structure to hold pressure reading with timestamp
struct PressureReading {
    time_t timestamp;
    float pressure;
};

class PressureLogger {
private:
    static const char* LOG_FILE;
    static const size_t MAX_READINGS = 1440; // Store up to 24 hours of readings at 1-minute intervals
    static const float PRESSURE_CHANGE_THRESHOLD; // Record if pressure changes by this amount
    
    TimeManager& timeManager;
    std::vector<PressureReading> readings;
    bool initialized;
    float lastRecordedPressure;
    unsigned long lastSaveTime;
    const unsigned long saveInterval = 300000; // Save to file every 5 minutes
    
    bool loadReadings();
    bool saveReadings();
    void trimOldReadings(size_t maxEntries);
    
public:
    PressureLogger(TimeManager& tm);
    
    void begin();
    void addReading(float pressure);
    void update(); // Call this regularly to check if we need to save
    
    // Get readings for web display
    String getReadingsAsJson();
    String getReadingsAsHtml();
    
    // Clear all readings
    bool clearReadings();
    
    // Check available space
    bool checkSpaceAndTrim();
    
    // Static method to check space on filesystem
    static bool checkFileSystemSpace();
};

#endif // PRESSURELOGGER_H
