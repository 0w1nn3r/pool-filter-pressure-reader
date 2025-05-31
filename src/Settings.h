#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

class Settings {
private:
    Preferences preferences;
    bool initialized;
    
    // Default values
    static constexpr float DEFAULT_BACKFLUSH_THRESHOLD = 2.0f;
    static constexpr unsigned int DEFAULT_BACKFLUSH_DURATION = 30;
    static constexpr float DEFAULT_SENSOR_MAX_PRESSURE = 4.0f;
    static constexpr unsigned int DEFAULT_DATA_RETENTION_DAYS = 7;
    
    // Namespace and keys
    static constexpr const char* NAMESPACE = "poolfilter";
    static constexpr const char* KEY_THRESHOLD = "threshold";
    static constexpr const char* KEY_DURATION = "duration";
    static constexpr const char* KEY_SENSOR_MAX = "sensormax";
    static constexpr const char* KEY_RETENTION_DAYS = "retdays";
    
    void setDefaults();

public:
    Settings();
    
    void begin();
    void reset();
    
    // Getters and setters
    float getBackflushThreshold();
    unsigned int getBackflushDuration();
    float getSensorMaxPressure();
    unsigned int getDataRetentionDays();
    void setBackflushThreshold(float threshold);
    void setBackflushDuration(unsigned int duration);
    void setSensorMaxPressure(float maxPressure);
    void setDataRetentionDays(unsigned int days);
};

#endif // SETTINGS_H
