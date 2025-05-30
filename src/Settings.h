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
    
    // Namespace and keys
    static constexpr const char* NAMESPACE = "poolfilter";
    static constexpr const char* KEY_THRESHOLD = "threshold";
    static constexpr const char* KEY_DURATION = "duration";
    
    void setDefaults();

public:
    Settings();
    
    void begin();
    void reset();
    
    // Getters and setters
    float getBackflushThreshold();
    unsigned int getBackflushDuration();
    
    void setBackflushThreshold(float threshold);
    void setBackflushDuration(unsigned int duration);
};

#endif // SETTINGS_H
