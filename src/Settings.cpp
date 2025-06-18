#include "Settings.h"
#include <LittleFS.h>

// Default calibration points (voltage, pressure)
const CalibrationPoint Settings::DEFAULT_CALIBRATION[NUM_CALIBRATION_POINTS] = {
    {0.4f, 0.0f},
    {0.54f, 0.94f},  // 0.9 bar at 0.54V
    {0.57f, 1.0f},   // 1.0 bar at 0.57V
    {0.63f, 1.2f},   // 1.2 bar at 0.63V
    {0.65f, 1.3f},   // 1.3 bar at 0.65V
    {0.68f, 1.4f},   // 1.4 bar at 0.68V
    {0.685f, 1.5f},  // 1.5 bar at 0.685V
    {0.715f, 1.6f},  // 1.6 bar at 0.715V
    {0.725f, 1.7f},  // 1.7 bar at 0.725V
    {0.78f, 2.0f}    // 2.0 bar at 0.78V
};

Settings::Settings() : initialized(false) {
    // Initialize with default calibration
    memcpy(calibrationTable, DEFAULT_CALIBRATION, sizeof(DEFAULT_CALIBRATION));
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
    
    // Load calibration from preferences
    loadCalibration();
}

void Settings::setDefaults() {
    setBackflushThreshold(DEFAULT_BACKFLUSH_THRESHOLD);
    setBackflushDuration(DEFAULT_BACKFLUSH_DURATION);
    setSensorMaxPressure(DEFAULT_SENSOR_MAX_PRESSURE);
    setDataRetentionDays(DEFAULT_DATA_RETENTION_DAYS);
    
    // Reset to default calibration
    memcpy(calibrationTable, DEFAULT_CALIBRATION, sizeof(DEFAULT_CALIBRATION));
    saveCalibration();
    
    // Set default pressure change threshold
    setPressureChangeThreshold(DEFAULT_PRESSURE_CHANGE_THRESHOLD);
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

// Calibration table methods
bool Settings::setCalibrationPoint(int index, float voltage, float pressure) {
    if (index < 0 || index >= NUM_CALIBRATION_POINTS) {
        return false;
    }
    
    // Validate voltage is in ascending order
    if (index > 0 && voltage <= calibrationTable[index-1].voltage) {
        return false;
    }
    if (index < NUM_CALIBRATION_POINTS-1 && voltage >= calibrationTable[index+1].voltage) {
        return false;
    }
    
    calibrationTable[index].voltage = voltage;
    calibrationTable[index].pressure = pressure;
    return true;
}

// Save calibration to preferences
bool Settings::saveCalibration() {
    if (!initialized) return false;
    
    size_t written = preferences.putBytes(KEY_CALIBRATION, calibrationTable, 
                                         sizeof(CalibrationPoint) * NUM_CALIBRATION_POINTS);
    return written == sizeof(CalibrationPoint) * NUM_CALIBRATION_POINTS;
}

// Load calibration from preferences
bool Settings::loadCalibration() {
    if (!initialized) return false;
    
    size_t size = preferences.getBytesLength(KEY_CALIBRATION);
    if (size == sizeof(CalibrationPoint) * NUM_CALIBRATION_POINTS) {
        size_t read = preferences.getBytes(KEY_CALIBRATION, calibrationTable, 
                                          sizeof(CalibrationPoint) * NUM_CALIBRATION_POINTS);
        return read == sizeof(CalibrationPoint) * NUM_CALIBRATION_POINTS;
    }
    
    // If no saved calibration, use defaults
    memcpy(calibrationTable, DEFAULT_CALIBRATION, sizeof(DEFAULT_CALIBRATION));
    return false;
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
    
    if (maxPressure >= 1.0 && maxPressure <= 30.0) {  // 1 to 30 bar range for sensors
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

float Settings::getPressureChangeThreshold() {
    if (!initialized) begin();
    return preferences.getFloat(KEY_PRESSURE_CHANGE_THRESHOLD, DEFAULT_PRESSURE_CHANGE_THRESHOLD);
}

void Settings::setPressureChangeThreshold(float threshold) {
    if (!initialized) begin();
    preferences.putFloat(KEY_PRESSURE_CHANGE_THRESHOLD, threshold);
}

unsigned int Settings::getPressureChangeMaxInterval() {
    if (!initialized) begin();
    return preferences.getUInt(KEY_PRESSURE_CHANGE_MAX_INTERVAL, DEFAULT_PRESSURE_CHANGE_MAX_INTERVAL);
}

void Settings::setPressureChangeMaxInterval(unsigned int interval) {
    if (!initialized) begin();
    preferences.putUInt(KEY_PRESSURE_CHANGE_MAX_INTERVAL, interval);
}