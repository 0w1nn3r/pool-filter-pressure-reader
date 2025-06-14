#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

// Number of calibration points
const int NUM_CALIBRATION_POINTS = 10;

// Structure to hold a calibration point
typedef struct {
    float voltage;
    float pressure;
} CalibrationPoint;

class Settings {
private:
    Preferences preferences;
    bool initialized;
    
    // Default values
    static constexpr float DEFAULT_BACKFLUSH_THRESHOLD = 2.0f;
    static constexpr unsigned int DEFAULT_BACKFLUSH_DURATION = 30;
    static constexpr float DEFAULT_SENSOR_MAX_PRESSURE = 4.0f;
    static constexpr unsigned int DEFAULT_DATA_RETENTION_DAYS = 7;
    
    // Default calibration points (voltage, pressure)
    static const CalibrationPoint DEFAULT_CALIBRATION[NUM_CALIBRATION_POINTS];
    
    // Namespace and keys
    static constexpr const char* NAMESPACE = "poolfilter";
    static constexpr const char* KEY_THRESHOLD = "threshold";
    static constexpr const char* KEY_DURATION = "duration";
    static constexpr const char* KEY_SENSOR_MAX = "sensormax";
    static constexpr const char* KEY_RETENTION_DAYS = "retdays";
    static constexpr const char* KEY_CALIBRATION = "cal";
    
    void setDefaults();

public:
    // Current calibration table
    CalibrationPoint calibrationTable[NUM_CALIBRATION_POINTS];
    
    Settings();
    
    void begin();
    void reset();
    
    // Getters and setters
    float getBackflushThreshold();
    unsigned int getBackflushDuration();
    
    // Getters and setters
    float getSensorMaxPressure();
    unsigned int getDataRetentionDays();
    
    // Calibration methods
    const CalibrationPoint* getCalibrationTable() const { return calibrationTable; }
    bool setCalibrationPoint(int index, float voltage, float pressure);
    bool saveCalibration();
    bool loadCalibration();
    
    void setBackflushThreshold(float threshold);
    void setBackflushDuration(unsigned int duration);
    void setSensorMaxPressure(float maxPressure);
    void setDataRetentionDays(unsigned int days);
};

#endif // SETTINGS_H
