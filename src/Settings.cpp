#include "Settings.h"
#include <LittleFS.h>

Settings::Settings() : initialized(false) {
}

void Settings::begin() {
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    // Open preferences with namespace "poolfilter"
    preferences.begin(NAMESPACE, false); // false = read/write mode
    initialized = true;
}

void Settings::setDefaults() {
    setBackflushThreshold(DEFAULT_BACKFLUSH_THRESHOLD);
    setBackflushDuration(DEFAULT_BACKFLUSH_DURATION);
    setSensorMaxPressure(DEFAULT_SENSOR_MAX_PRESSURE);
    setDataRetentionDays(DEFAULT_DATA_RETENTION_DAYS);
}

void Settings::reset() {
    // Close preferences and unmount filesystem
    preferences.end();
    LittleFS.end();
    
    // Format the entire filesystem
    LittleFS.format();
    
    // Reinitialize filesystem and preferences
    LittleFS.begin();
    preferences.begin(NAMESPACE, false);
    
    // Set defaults
    setDefaults();
}

float Settings::getBackflushThreshold() {
    if (!initialized) {
        return DEFAULT_BACKFLUSH_THRESHOLD;
    }
    
    // Get threshold with default value if not found
    return preferences.getFloat(KEY_THRESHOLD, DEFAULT_BACKFLUSH_THRESHOLD);
}

unsigned int Settings::getBackflushDuration() {
    if (!initialized) {
        return DEFAULT_BACKFLUSH_DURATION;
    }
    
    // Get duration with default value if not found
    return preferences.getUInt(KEY_DURATION, DEFAULT_BACKFLUSH_DURATION);
}

void Settings::setBackflushThreshold(float threshold) {
    if (!initialized) {
        return;
    }
    
    if (threshold >= 0.2 && threshold <= 4.0) {  // Assuming 4.0 bar is max
        preferences.putFloat(KEY_THRESHOLD, threshold);
    }
}

void Settings::setBackflushDuration(unsigned int duration) {
    if (!initialized) {
        return;
    }
    
    if (duration >= 5 && duration <= 300) {  // 5s to 5min
        preferences.putUInt(KEY_DURATION, duration);
    }
}

float Settings::getSensorMaxPressure() {
    if (!initialized) {
        return DEFAULT_SENSOR_MAX_PRESSURE;
    }
    
    // Get sensor max pressure with default value if not found
    return preferences.getFloat(KEY_SENSOR_MAX, DEFAULT_SENSOR_MAX_PRESSURE);
}

void Settings::setSensorMaxPressure(float maxPressure) {
    if (!initialized) {
        return;
    }
    
    if (maxPressure >= 1.0 && maxPressure <= 10.0) {  // 1 to 10 bar range for sensors
        preferences.putFloat(KEY_SENSOR_MAX, maxPressure);
    }
}

unsigned int Settings::getDataRetentionDays() {
    if (!initialized) {
        return DEFAULT_DATA_RETENTION_DAYS;
    }
    
    // Get retention days with default value if not found
    return preferences.getUInt(KEY_RETENTION_DAYS, DEFAULT_DATA_RETENTION_DAYS);
}

void Settings::setDataRetentionDays(unsigned int days) {
    if (!initialized) {
        return;
    }
    
    // Limit to reasonable range (1-90 days)
    if (days >= 1 && days <= 90) {
        preferences.putUInt(KEY_RETENTION_DAYS, days);
    }
}
