#ifndef BACKFLUSHLOGGER_H
#define BACKFLUSHLOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "TimeManager.h"

// Structure to hold a backflush event
struct BackflushEvent {
    time_t timestamp;
    float pressure;
    unsigned int duration;
};

class BackflushLogger {
private:
    static const char* LOG_FILE;
    static const size_t MAX_EVENTS = 50;  // Maximum number of events to store
    
    TimeManager& timeManager;
    std::vector<BackflushEvent> events;
    bool initialized;
    
    bool loadEvents();
    bool saveEvents();

public:
    BackflushLogger(TimeManager& tm);
    
    void begin();
    bool logEvent(float pressure, unsigned int duration);
    String getEventsAsJson();
    String getEventsAsHtml();
    void clearEvents();
    size_t getEventCount() const { return events.size(); }
};

#endif // BACKFLUSHLOGGER_H
