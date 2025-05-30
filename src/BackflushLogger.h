#ifndef BACKFLUSHLOGGER_H
#define BACKFLUSHLOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "TimeManager.h"

// Structure to hold backflush event data
struct BackflushEvent {
    time_t timestamp;
    float pressure;
    unsigned int duration;
};

class BackflushLogger {
private:
    static const char* LOG_FILE;
    static const size_t MAX_EVENTS = 100; // Maximum number of events to store
    
    TimeManager& timeManager;
    std::vector<BackflushEvent> events;
    bool initialized;
    
    bool loadEvents();
    bool saveEvents();
    void trimOldEvents(size_t maxEvents);
    
public:
    BackflushLogger(TimeManager& tm);
    
    void begin();
    void logEvent(float pressure, unsigned int duration);
    
    // Get events for web display
    String getEventsAsJson();
    String getEventsAsHtml();
    
    // Clear all events
    bool clearEvents();
    
    // Get event count
    size_t getEventCount() const { return events.size(); }
    
    // Check available space
    bool checkSpaceAndTrim();
    
    // Static method to check space on filesystem
    static bool checkFileSystemSpace();
};

#endif // BACKFLUSHLOGGER_H
