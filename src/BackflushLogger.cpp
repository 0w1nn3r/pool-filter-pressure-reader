#include "BackflushLogger.h"

const char* BackflushLogger::LOG_FILE = "/backflush_log.json";

BackflushLogger::BackflushLogger(TimeManager& tm) 
    : timeManager(tm), initialized(false) {
}

void BackflushLogger::begin() {
    // Initialize file system
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }
    
    // Load existing events
    if (loadEvents()) {
        Serial.println("Backflush events loaded successfully");
        Serial.print("Number of events: ");
        Serial.println(events.size());
    } else {
        Serial.println("No backflush events found or error loading events");
        events.clear();
    }
    
    initialized = true;
}

bool BackflushLogger::loadEvents() {
    // Check if file exists
    if (!LittleFS.exists(LOG_FILE)) {
        return false;
    }
    
    // Open file for reading
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file) {
        return false;
    }
    
    // Parse JSON
    DynamicJsonDocument doc(16384); // Adjust size as needed
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("Failed to parse backflush log: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Clear existing events
    events.clear();
    
    // Read events from JSON
    JsonArray eventsArray = doc["events"].as<JsonArray>();
    for (JsonObject eventObj : eventsArray) {
        BackflushEvent event;
        event.timestamp = eventObj["timestamp"].as<time_t>();
        event.pressure = eventObj["pressure"].as<float>();
        event.duration = eventObj["duration"].as<unsigned int>();
        events.push_back(event);
    }
    
    return true;
}

bool BackflushLogger::saveEvents() {
    // Create JSON document
    DynamicJsonDocument doc(16384); // Adjust size as needed
    
    // Create events array
    JsonArray eventsArray = doc.createNestedArray("events");
    
    // Add events to array
    for (const BackflushEvent& event : events) {
        JsonObject eventObj = eventsArray.createNestedObject();
        eventObj["timestamp"] = event.timestamp;
        eventObj["pressure"] = event.pressure;
        eventObj["duration"] = event.duration;
    }
    
    // Open file for writing
    File file = LittleFS.open(LOG_FILE, "w");
    if (!file) {
        return false;
    }
    
    // Serialize JSON to file
    if (serializeJson(doc, file) == 0) {
        file.close();
        return false;
    }
    
    file.close();
    return true;
}

bool BackflushLogger::logEvent(float pressure, unsigned int duration) {
    if (!initialized || !timeManager.isTimeInitialized()) {
        return false;
    }
    
    // Create new event
    BackflushEvent event;
    event.timestamp = timeManager.getCurrentTime();
    event.pressure = pressure;
    event.duration = duration;
    
    // Add to events list
    events.push_back(event);
    
    // If we have too many events, remove the oldest one
    if (events.size() > MAX_EVENTS) {
        events.erase(events.begin());
    }
    
    // Save events to file
    return saveEvents();
}

String BackflushLogger::getEventsAsJson() {
    DynamicJsonDocument doc(16384); // Adjust size as needed
    
    // Create events array
    JsonArray eventsArray = doc.createNestedArray("events");
    
    // Add events to array
    for (const BackflushEvent& event : events) {
        JsonObject eventObj = eventsArray.createNestedObject();
        eventObj["timestamp"] = event.timestamp;
        
        // Format date and time
        char dateTime[20];
        time_t t = event.timestamp;
        struct tm* timeinfo = localtime(&t);
        sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d",
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        eventObj["datetime"] = String(dateTime);
        
        eventObj["pressure"] = event.pressure;
        eventObj["duration"] = event.duration;
    }
    
    // Serialize JSON to string
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

String BackflushLogger::getEventsAsHtml() {
    String html = "<table class='events-table'>\n";
    html += "  <tr>\n";
    html += "    <th>Date</th>\n";
    html += "    <th>Time</th>\n";
    html += "    <th>Pressure (bar)</th>\n";
    html += "    <th>Duration (sec)</th>\n";
    html += "  </tr>\n";
    
    // Sort events by timestamp (newest first)
    std::vector<BackflushEvent> sortedEvents = events;
    std::sort(sortedEvents.begin(), sortedEvents.end(), 
              [](const BackflushEvent& a, const BackflushEvent& b) {
                  return a.timestamp > b.timestamp;
              });
    
    for (const BackflushEvent& event : sortedEvents) {
        time_t t = event.timestamp;
        struct tm* timeinfo = localtime(&t);
        
        char date[11];
        sprintf(date, "%04d-%02d-%02d", 
                timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
        
        char time[9];
        sprintf(time, "%02d:%02d:%02d", 
                timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        
        html += "  <tr>\n";
        html += "    <td>" + String(date) + "</td>\n";
        html += "    <td>" + String(time) + "</td>\n";
        html += "    <td>" + String(event.pressure, 1) + "</td>\n";
        html += "    <td>" + String(event.duration) + "</td>\n";
        html += "  </tr>\n";
    }
    
    html += "</table>\n";
    
    if (events.empty()) {
        html = "<p>No backflush events recorded yet.</p>";
    }
    
    return html;
}

void BackflushLogger::clearEvents() {
    events.clear();
    saveEvents();
}
