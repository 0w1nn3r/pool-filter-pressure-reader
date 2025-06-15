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
        
        // Load type if available, default to "Auto" for backward compatibility
        if (eventObj.containsKey("type")) {
            event.type = eventObj["type"].as<String>();
        } else {
            event.type = "Auto";
        }
        
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
        eventObj["type"] = event.type.length() > 0 ? event.type : "Auto";
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

void BackflushLogger::logEvent(float pressure, unsigned int duration, const String& type) {
    if (!initialized || !timeManager.isTimeInitialized()) {
        return;
    }
    
    // Create new event with GMT timestamp
    BackflushEvent event;
    event.timestamp = timeManager.getCurrentGMTTime();
    event.pressure = pressure;
    event.duration = duration;
    event.type = type;
    
    // Add to events list
    events.push_back(event);
    
    // If we have too many events, trim them
    if (events.size() > MAX_EVENTS) {
        trimOldEvents(MAX_EVENTS);
    }
    
    // Check available space
    checkSpaceAndTrim();
    
    // Save events to file
    saveEvents();
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
        time_t localt = timeManager.gmtToLocal(t);
        struct tm* timeinfo = localtime(&localt);

        strftime(dateTime, sizeof(dateTime), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        eventObj["datetime"] = String(dateTime);
        
        eventObj["pressure"] = event.pressure;
        eventObj["duration"] = event.duration;
        eventObj["type"] = event.type.length() > 0 ? event.type : "Auto";
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
    html += "    <th>Type</th>\n";
    html += "  </tr>\n";
    
    // Sort events by timestamp (newest first)
    std::vector<BackflushEvent> sortedEvents = events;
    std::sort(sortedEvents.begin(), sortedEvents.end(), 
              [](const BackflushEvent& a, const BackflushEvent& b) {
                  return a.timestamp > b.timestamp;
              });
    
    for (const BackflushEvent& event : sortedEvents) {
        time_t t = timeManager.gmtToLocal(event.timestamp);
        
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
        html += "    <td>" + (event.type.length() > 0 ? event.type : "Auto") + "</td>\n";
        html += "  </tr>\n";
    }
    
    html += "</table>\n";
    
    if (events.empty()) {
        html = "<p>No backflush events recorded yet.</p>";
    }
    
    return html;
}

bool BackflushLogger::clearEvents() {
    // Clear events
    events.clear();
    
    // Delete file
    if (LittleFS.exists(LOG_FILE)) {
        if (!LittleFS.remove(LOG_FILE)) {
            Serial.println("Failed to delete backflush log file");
            return false;
        }
    }
    
    return true;
}

void BackflushLogger::trimOldEvents(size_t maxEvents) {
    if (events.size() <= maxEvents) {
        return;
    }
    
    // Calculate how many events to remove
    size_t entriesToRemove = events.size() - maxEvents;
    
    // Remove oldest events
    events.erase(events.begin(), events.begin() + entriesToRemove);
    
    Serial.print("Trimmed ");
    Serial.print(entriesToRemove);
    Serial.println(" old backflush events");
}

bool BackflushLogger::checkSpaceAndTrim() {
    // Check if filesystem space is low
    if (checkFileSystemSpace()) {
        Serial.println("Low space detected, trimming backflush logs");
        
        // If we have events, trim them
        if (!events.empty()) {
            // Keep only half of the events
            size_t keepEvents = events.size() / 2;
            if (keepEvents < 10) {
                keepEvents = 10; // Keep at least 10 events
            }
            trimOldEvents(keepEvents);
            saveEvents();
        }
        
        return true;
    }
    
    return false;
}

bool BackflushLogger::checkFileSystemSpace() {
    FSInfo fs_info;
    if (!LittleFS.info(fs_info)) {
        Serial.println("Failed to get filesystem info");
        return false;
    }
    
    // Calculate available space in KB
    size_t freeSpace = fs_info.totalBytes - fs_info.usedBytes;
    size_t freeSpaceKB = freeSpace / 1024;
    
    Serial.print("LittleFS: ");
    Serial.print(fs_info.usedBytes / 1024);
    Serial.print("KB used, ");
    Serial.print(freeSpaceKB);
    Serial.print("KB free, ");
    Serial.print(fs_info.totalBytes / 1024);
    Serial.println("KB total");
    
    // Return true if free space is less than 10% of total
    return (freeSpace < (fs_info.totalBytes / 10));
}
