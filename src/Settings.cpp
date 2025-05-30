#include "Settings.h"

Settings::Settings() : initialized(false) {
}

void Settings::begin() {
    // Open preferences with namespace "poolfilter"
    preferences.begin(NAMESPACE, false); // false = read/write mode
    initialized = true;
}

void Settings::setDefaults() {
    setBackflushThreshold(DEFAULT_BACKFLUSH_THRESHOLD);
    setBackflushDuration(DEFAULT_BACKFLUSH_DURATION);
}

void Settings::reset() {
    // Clear all preferences
    preferences.clear();
    
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
    
    if (threshold >= 0.5 && threshold <= 4.0) {  // Assuming 4.0 bar is max
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
