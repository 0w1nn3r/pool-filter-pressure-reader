#ifndef BACKFLUSHSCHEDULER_H
#define BACKFLUSHSCHEDULER_H

#include <Arduino.h>
#include <vector>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "TimeManager.h"

// Maximum number of schedules allowed
#define MAX_SCHEDULES 3

// Schedule types
enum class ScheduleType {
    DAILY,    // Every day at specific time
    WEEKLY,   // Specific days of week at specific time
    MONTHLY   // Specific day of month at specific time
};

// Structure to hold schedule data
struct BackflushSchedule {
    bool enabled;                // Whether this schedule is active
    ScheduleType type;           // Type of schedule (daily, weekly, monthly)
    uint8_t hour;                // Hour (0-23)
    uint8_t minute;              // Minute (0-59)
    uint16_t daysActive;         // Bitmap for days (weekly: bit 0=Sunday, monthly: day 1-31)
    uint16_t duration;           // Duration in seconds
    
    // Constructor with defaults
    BackflushSchedule() : 
        enabled(false),
        type(ScheduleType::DAILY),
        hour(0),
        minute(0),
        daysActive(0),
        duration(30) {}
};

class BackflushScheduler {
private:
    static const char* SCHEDULE_FILE;
    TimeManager& timeManager;
    std::vector<BackflushSchedule> schedules;
    bool initialized;
    unsigned long lastCheckTime;
    
    bool loadSchedules();
    bool saveSchedules();
    
public:
    BackflushScheduler(TimeManager& tm);
    
    void begin();
    
    // Schedule management
    bool addSchedule(const BackflushSchedule& schedule);
    bool updateSchedule(size_t index, const BackflushSchedule& schedule);
    bool deleteSchedule(size_t index);
    bool clearSchedules();
    
    // Get schedules
    size_t getScheduleCount() const { return schedules.size(); }
    BackflushSchedule getSchedule(size_t index) const;
    std::vector<BackflushSchedule> getAllSchedules() const { return schedules; }
    
    // Schedule checking
    bool checkSchedules(time_t currentTime, unsigned int& scheduledDuration);
    
    // Get next scheduled backflush time
    bool getNextScheduledTime(time_t& nextTime, unsigned int& duration) const;
    
    // Convert schedule to JSON for web display
    String getSchedulesAsJson() const;
};

#endif // BACKFLUSHSCHEDULER_H
