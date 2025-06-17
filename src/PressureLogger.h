#ifndef PRESSURELOGGER_H
#define PRESSURELOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "TimeManager.h"
#include "Settings.h"

// Structure to hold pressure reading with timestamp
struct PressureReading {
    time_t timestamp;
    float pressure;
};

class PressureLogger {
private:
    static const char* LOG_FILE;
    static const size_t MAX_READINGS = 500; 
    static const float PRESSURE_CHANGE_THRESHOLD; // Record if pressure changes by this amount
    
    TimeManager& timeManager;
    Settings* settings; // Reference to settings for data retention period
    std::vector<PressureReading> readings;
    bool initialized;
    float lastRecordedPressure;
    unsigned long lastSaveTime;
    const unsigned long saveInterval = 300000; // Save to file every 5 minutes
    
    bool loadReadings();
    void trimOldReadings(size_t maxEntries);
    
public:
    PressureLogger(TimeManager& tm, Settings& settings);
    
    void begin();
    void addReading(float pressure, bool force = false);
    void addReadingWithTimestamp(const PressureReading& reading); // Add reading with explicit timestamp
    bool saveReadings(); // Made public for forced saves
    void update(); // Call this regularly to check if we need to save
    
    // Get readings for web display
    String getReadingsAsJson();
    
    // Get paginated readings for web display
    String getPaginatedReadingsAsJson(int page, int limit, int& totalPages);
    
    // Clear all readings
    bool clearReadings();
    
    // Check available space
    bool checkSpaceAndTrim();
    
    // Get readings as CSV
    String getReadingsAsCsv();
    
    // Get number of readings
    size_t getReadingCount() { return readings.size(); }
    
    // Set settings reference (used when settings are updated)
    void setSettings(Settings& settings) { this->settings = &settings; }
    
    // Prune data older than retention period
    void pruneOldData();
    
    // Static method to check space on filesystem
    static bool checkFileSystemSpace();
};

#endif // PRESSURELOGGER_H
