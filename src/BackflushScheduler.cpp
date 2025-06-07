#include "BackflushScheduler.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

const char* BackflushScheduler::SCHEDULE_FILE = "/schedules.json";

BackflushScheduler::BackflushScheduler(TimeManager& tm)
    : timeManager(tm), initialized(false), lastCheckTime(0) {
}

void BackflushScheduler::begin() {
    // Initialize LittleFS if not already initialized
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS for scheduler");
        return;
    }
    
    // Load existing schedules
    if (loadSchedules()) {
        Serial.print("Loaded ");
        Serial.print(schedules.size());
        Serial.println(" backflush schedules");
    } else {
        Serial.println("No schedules found or error loading schedules");
    }
    
    initialized = true;
}

bool BackflushScheduler::loadSchedules() {
    // Check if schedule file exists
    if (!LittleFS.exists(SCHEDULE_FILE)) {
        Serial.println("Schedule file does not exist");
        return false;
    }
    
    // Open file for reading
    File file = LittleFS.open(SCHEDULE_FILE, "r");
    if (!file) {
        Serial.println("Failed to open schedule file for reading");
        return false;
    }
    
    // Allocate JsonDocument
    JsonDocument doc;
    
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("Failed to parse schedule file: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Clear existing schedules
    schedules.clear();
    
    // Read schedules array
    JsonArray schedulesArray = doc["schedules"].as<JsonArray>();
    for (JsonObject scheduleObj : schedulesArray) {
        if (schedules.size() >= MAX_SCHEDULES) {
            Serial.println("Maximum number of schedules reached, ignoring additional schedules");
            break;
        }
        
        BackflushSchedule schedule;
        schedule.enabled = scheduleObj["enabled"] | false;
        
        // Parse schedule type
        String typeStr = scheduleObj["type"].as<String>();
        if (typeStr == "daily") {
            schedule.type = ScheduleType::DAILY;
        } else if (typeStr == "weekly") {
            schedule.type = ScheduleType::WEEKLY;
        } else if (typeStr == "monthly") {
            schedule.type = ScheduleType::MONTHLY;
        } else {
            schedule.type = ScheduleType::DAILY; // Default to daily if unknown
        }
        
        schedule.hour = scheduleObj["hour"] | 0;
        schedule.minute = scheduleObj["minute"] | 0;
        schedule.daysActive = scheduleObj["daysActive"] | 0;
        schedule.duration = scheduleObj["duration"] | 30;
        
        schedules.push_back(schedule);
    }
    
    return true;
}

bool BackflushScheduler::saveSchedules() {
    if (!initialized) {
        Serial.println("Scheduler not initialized, cannot save schedules");
        return false;
    }
    
    // Create JSON document
    JsonDocument doc;
    JsonArray schedulesArray = doc["schedules"].to<JsonArray>();
    
    // Add each schedule to the array
    for (const BackflushSchedule& schedule : schedules) {
        JsonObject scheduleObj = schedulesArray.add<JsonObject>();
        scheduleObj["enabled"] = schedule.enabled;
        
        // Convert schedule type to string
        switch (schedule.type) {
            case ScheduleType::DAILY:
                scheduleObj["type"] = "daily";
                break;
            case ScheduleType::WEEKLY:
                scheduleObj["type"] = "weekly";
                break;
            case ScheduleType::MONTHLY:
                scheduleObj["type"] = "monthly";
                break;
        }
        
        scheduleObj["hour"] = schedule.hour;
        scheduleObj["minute"] = schedule.minute;
        scheduleObj["daysActive"] = schedule.daysActive;
        scheduleObj["duration"] = schedule.duration;
    }
    
    // Open file for writing
    File file = LittleFS.open(SCHEDULE_FILE, "w");
    if (!file) {
        Serial.println("Failed to open schedule file for writing");
        return false;
    }
    
    // Serialize JSON to file
    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write schedule file");
        file.close();
        return false;
    }
    
    file.close();
    return true;
}

bool BackflushScheduler::addSchedule(const BackflushSchedule& schedule) {
    if (!initialized) {
        return false;
    }
    
    // Check if we've reached the maximum number of schedules
    if (schedules.size() >= MAX_SCHEDULES) {
        Serial.println("Maximum number of schedules reached");
        return false;
    }
    
    // Add the new schedule
    schedules.push_back(schedule);
    
    // Save the updated schedules
    return saveSchedules();
}

bool BackflushScheduler::updateSchedule(size_t index, const BackflushSchedule& schedule) {
    if (!initialized || index >= schedules.size()) {
        return false;
    }
    
    // Update the schedule at the specified index
    schedules[index] = schedule;
    
    // Save the updated schedules
    return saveSchedules();
}

bool BackflushScheduler::deleteSchedule(size_t index) {
    if (!initialized || index >= schedules.size()) {
        return false;
    }
    
    // Remove the schedule at the specified index
    schedules.erase(schedules.begin() + index);
    
    // Save the updated schedules
    return saveSchedules();
}

bool BackflushScheduler::clearSchedules() {
    if (!initialized) {
        return false;
    }
    
    // Clear all schedules
    schedules.clear();
    
    // Save the updated schedules
    return saveSchedules();
}

BackflushSchedule BackflushScheduler::getSchedule(size_t index) const {
    if (index >= schedules.size()) {
        // Return an empty schedule if the index is out of range
        return BackflushSchedule();
    }
    
    return schedules[index];
}

bool BackflushScheduler::checkSchedules(time_t currentTime, unsigned int& scheduledDuration) {
    if (!initialized || schedules.empty()) {
        return false;
    }
    
    // Apply cooldown period (5 minutes) to prevent multiple triggers
    static time_t lastTriggerTime = 0;
    const time_t COOLDOWN_PERIOD = 5 * 60; // 5 minutes in seconds
    
    // If we've triggered recently, don't trigger again
    if (lastTriggerTime > 0 && (currentTime - lastTriggerTime) < COOLDOWN_PERIOD) {
        return false;
    }
    
    struct tm* timeinfo = localtime(&currentTime);
    int currentHour = timeinfo->tm_hour;
    int currentMinute = timeinfo->tm_min;
    int currentSecond = timeinfo->tm_sec;
    int currentWeekday = timeinfo->tm_wday; // 0 = Sunday, 6 = Saturday
    int currentMonthday = timeinfo->tm_mday; // 1-31
    
    bool shouldBackflush = false;
    scheduledDuration = 0;
    
    // Check each schedule
    for (const BackflushSchedule& schedule : schedules) {
        if (!schedule.enabled) {
            continue;
        }
        
        bool scheduleMatches = false;
        
        // Check if the current time matches the schedule
        if (currentHour == schedule.hour && currentMinute == schedule.minute && currentSecond < 60) {
            // Time matches, now check the day based on schedule type
            switch (schedule.type) {
                case ScheduleType::DAILY:
                    // Daily schedule always matches
                    scheduleMatches = true;
                    break;
                    
                case ScheduleType::WEEKLY:
                    // Check if the current weekday is in the active days bitmap
                    // Bit 0 = Sunday, Bit 1 = Monday, etc.
                    scheduleMatches = (schedule.daysActive & (1 << currentWeekday)) != 0;
                    break;
                    
                case ScheduleType::MONTHLY:
                    // Check if the current day of month is in the active days bitmap
                    // We subtract 1 because days are 1-31 but bitmap is 0-based
                    scheduleMatches = (schedule.daysActive & (1 << (currentMonthday - 1))) != 0;
                    break;
            }
        }
        
        // If this schedule matches and has a longer duration, use it
        if (scheduleMatches) {
            shouldBackflush = true;
            scheduledDuration = (schedule.duration > scheduledDuration) ? (unsigned int)schedule.duration : scheduledDuration;
            
            // Log the scheduled backflush
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
            Serial.print("Scheduled backflush triggered at ");
            Serial.print(timeStr);
            Serial.print(" with duration ");
            Serial.print(scheduledDuration);
            Serial.println(" seconds");
            
            // Update last trigger time
            lastTriggerTime = currentTime;
        }
    }
    
    return shouldBackflush;
}

// Helper function to get days in a month (1-12, year with century)
static int getDaysInMonth(int month, int year) {
    if (month < 1 || month > 12) return 31; // Default to 31 for invalid months
    
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 400 == 0 || (year % 100 != 0 && year % 4 == 0))) {
        return 29; // Leap year
    }
    return daysInMonth[month - 1];
}

bool BackflushScheduler::getNextScheduledTime(time_t& nextTime, unsigned int& duration) const {
    if (!initialized || schedules.empty() || !timeManager.isTimeInitialized()) {
        return false;
    }
    
    time_t currentTime = timeManager.getCurrentTime();
    time_t earliestTime = currentTime + 86400 * 31; // Initialize to a month from now
    bool foundSchedule = false;
    
    // Get current time components
    struct tm currentTm;
    localtime_r(&currentTime, &currentTm);
    int currentYear = currentTm.tm_year + 1900;
    int currentMonth = currentTm.tm_mon + 1;
    
    // Check each schedule
    for (const BackflushSchedule& schedule : schedules) {
        if (!schedule.enabled) {
            continue;
        }
        
        time_t scheduleTime = 0;
        
        switch (schedule.type) {
            case ScheduleType::DAILY: {
                // For daily, just find the next occurrence of the scheduled time
                struct tm nextTm = currentTm;
                nextTm.tm_hour = schedule.hour;
                nextTm.tm_min = schedule.minute;
                nextTm.tm_sec = 0;
                
                time_t nextOccurrence = mktime(&nextTm);
                if (nextOccurrence <= currentTime) {
                    nextOccurrence += 86400; // Add one day
                }
                scheduleTime = nextOccurrence;
                break;
            }
                
            case ScheduleType::WEEKLY: {
                // Find the next day of the week that matches
                struct tm nextTm = currentTm;
                nextTm.tm_hour = schedule.hour;
                nextTm.tm_min = schedule.minute;
                nextTm.tm_sec = 0;
                
                // Start checking from current day
                time_t baseTime = mktime(&nextTm);
                if (baseTime <= currentTime) {
                    // If time has passed today, start from tomorrow
                    nextTm.tm_mday++;
                    baseTime = mktime(&nextTm);
                }
                
                // Check up to 7 days forward
                for (int i = 0; i < 7; i++) {
                    struct tm checkTm;
                    localtime_r(&baseTime, &checkTm);
                    int checkWeekday = checkTm.tm_wday; // 0 = Sunday
                    
                    if (schedule.daysActive & (1 << checkWeekday)) {
                        // Found a matching day
                        scheduleTime = baseTime;
                        break;
                    }
                    
                    // Move to next day
                    baseTime += 86400;
                }
                break;
            }
                
            case ScheduleType::MONTHLY: {
                // Find the next day of the month that matches
                struct tm nextTm = currentTm;
                nextTm.tm_hour = schedule.hour;
                nextTm.tm_min = schedule.minute;
                nextTm.tm_sec = 0;
                
                // Check up to 12 months ahead
                for (int monthOffset = 0; monthOffset < 12; monthOffset++) {
                    int checkYear = currentYear + (currentMonth + monthOffset - 1) / 12;
                    int checkMonth = ((currentMonth - 1 + monthOffset) % 12) + 1;
                    int daysInMonth = getDaysInMonth(checkMonth, checkYear);
                    
                    // Check each day in the month
                    for (int day = 1; day <= daysInMonth; day++) {
                        // Skip days before current day if checking current month
                        if (monthOffset == 0 && day < currentTm.tm_mday) {
                            continue;
                        }
                        
                        // Check if this day is in the active days bitmap
                        if (schedule.daysActive & (1 << (day - 1))) {
                            // Found a matching day
                            struct tm matchTm = nextTm;
                            matchTm.tm_year = checkYear - 1900;
                            matchTm.tm_mon = checkMonth - 1;
                            matchTm.tm_mday = day;
                            
                            time_t matchTime = mktime(&matchTm);
                            
                            // Skip if this time is in the past
                            if (matchTime <= currentTime) {
                                continue;
                            }
                            
                            scheduleTime = matchTime;
                            break;
                        }
                    }
                    
                    if (scheduleTime != 0) {
                        break; // Found a match
                    }
                }
                break;
            }
        }
        
        // If we found a valid time and it's earlier than our current earliest
        if (scheduleTime > 0 && scheduleTime < earliestTime) {
            earliestTime = scheduleTime;
            duration = schedule.duration;
            foundSchedule = true;
        }
    }
    
    if (foundSchedule) {
        nextTime = earliestTime;
        return true;
    }
    
    return false;
}

String BackflushScheduler::getSchedulesAsJson() const {
    // Use JsonDocument instead of DynamicJsonDocument
    JsonDocument doc;
    JsonArray schedulesArray = doc["schedules"].to<JsonArray>();
    
    for (size_t i = 0; i < schedules.size(); i++) {
        const BackflushSchedule& schedule = schedules[i];
        JsonObject scheduleObj = schedulesArray.add<JsonObject>();
        
        scheduleObj["id"] = i;
        scheduleObj["enabled"] = schedule.enabled;
        
        // Convert schedule type to string
        switch (schedule.type) {
            case ScheduleType::DAILY:
                scheduleObj["type"] = "daily";
                break;
            case ScheduleType::WEEKLY:
                scheduleObj["type"] = "weekly";
                break;
            case ScheduleType::MONTHLY:
                scheduleObj["type"] = "monthly";
                break;
        }
        
        scheduleObj["hour"] = schedule.hour;
        scheduleObj["minute"] = schedule.minute;
        scheduleObj["daysActive"] = schedule.daysActive;
        scheduleObj["duration"] = schedule.duration;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}
