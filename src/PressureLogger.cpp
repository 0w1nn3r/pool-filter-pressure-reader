#include "PressureLogger.h"

const char* PressureLogger::LOG_FILE = "/pressure_history.json";
const float PressureLogger::PRESSURE_CHANGE_THRESHOLD = 0.1f; // Record if pressure changes by 0.1 bar or more

PressureLogger::PressureLogger(TimeManager& tm) 
    : timeManager(tm), initialized(false), lastRecordedPressure(0), lastSaveTime(0) {
}

void PressureLogger::begin() {
    // Initialize file system if not already initialized
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount file system in PressureLogger");
        return;
    }
    
    // Load existing readings
    if (loadReadings()) {
        Serial.println("Pressure readings loaded successfully");
        Serial.print("Number of readings: ");
        Serial.println(readings.size());
        
        // If we have readings, set the last recorded pressure
        if (!readings.empty()) {
            lastRecordedPressure = readings.back().pressure;
        }
    } else {
        Serial.println("No pressure readings found or error loading readings");
        readings.clear();
    }
    
    initialized = true;
    
    // Check space and trim if needed
    checkSpaceAndTrim();
}

bool PressureLogger::loadReadings() {
    // Check if file exists
    if (!LittleFS.exists(LOG_FILE)) {
        return false;
    }
    
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file) {
        Serial.println("Failed to open pressure log file for reading");
        return false;
    }
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.print("Failed to parse pressure log JSON: ");
        Serial.println(error.c_str());
        return false;
    }
    
    // Clear existing readings
    readings.clear();
    
    // Load readings from JSON
    JsonArray readingsArray = doc["readings"].as<JsonArray>();
    for (JsonObject readingObj : readingsArray) {
        PressureReading reading;
        reading.timestamp = readingObj["time"].as<time_t>();
        reading.pressure = readingObj["pressure"].as<float>();
        readings.push_back(reading);
    }
    
    return true;
}

bool PressureLogger::saveReadings() {
    // Check if initialized
    if (!initialized) {
        return false;
    }
    
    // Create JSON document
    JsonDocument doc;
    JsonArray readingsArray = doc["readings"].to<JsonArray>();
    
    // Add readings to JSON
    for (const PressureReading& reading : readings) {
        JsonObject readingObj = readingsArray.add<JsonObject>();
        readingObj["time"] = reading.timestamp;
        readingObj["pressure"] = reading.pressure;
    }
    
    // Open file for writing
    File file = LittleFS.open(LOG_FILE, "w");
    if (!file) {
        Serial.println("Failed to open pressure log file for writing");
        return false;
    }
    
    // Write JSON to file
    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write pressure log to file");
        file.close();
        return false;
    }
    
    file.close();
    lastSaveTime = millis();
    return true;
}

void PressureLogger::addReading(float pressure) {
    // Check if initialized
    if (!initialized) {
        return;
    }
    
    // Only record if pressure has changed significantly or it's the first reading
    if (readings.empty() || abs(pressure - lastRecordedPressure) >= PRESSURE_CHANGE_THRESHOLD) {
        PressureReading reading;
        reading.timestamp = timeManager.getCurrentTime();
        reading.pressure = pressure;
        
        readings.push_back(reading);
        lastRecordedPressure = pressure;
        
        // Trim old readings if we exceed maximum
        if (readings.size() > MAX_READINGS) {
            trimOldReadings(MAX_READINGS);
        }
        
        // Save immediately if this is the first reading or save interval has passed
        unsigned long currentTime = millis();
        if (readings.size() == 1 || currentTime - lastSaveTime >= saveInterval) {
            saveReadings();
        }
    }
}

void PressureLogger::update() {
    // Check if we need to save readings
    if (initialized && !readings.empty()) {
        unsigned long currentTime = millis();
        if (currentTime - lastSaveTime >= saveInterval) {
            saveReadings();
        }
    }
}

String PressureLogger::getReadingsAsJson() {
    // Create JSON document
    JsonDocument doc;
    JsonArray readingsArray = doc["readings"].to<JsonArray>();
    
    // Add readings to JSON
    for (const PressureReading& reading : readings) {
        JsonObject readingObj = readingsArray.add<JsonObject>();
        readingObj["time"] = reading.timestamp;
        readingObj["pressure"] = reading.pressure;
        
        // Add formatted time string
        char timeStr[20];
        struct tm* timeinfo = localtime(&reading.timestamp);
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
        readingObj["timeStr"] = String(timeStr);
        
        // Add formatted date string
        char dateStr[20];
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", timeinfo);
        readingObj["dateStr"] = String(dateStr);
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

String PressureLogger::getReadingsAsHtml() {
    String html = "<h2>Pressure History</h2>";
    
    if (readings.empty()) {
        html += "<p>No pressure readings recorded yet.</p>";
        return html;
    }
    
    // Add chart container
    html += "<div id='chart-container' style='width: 100%; height: 400px;'></div>";
    
    // Add JavaScript for chart
    html += "<script>";
    html += "var pressureData = " + getReadingsAsJson() + ";";
    html += "var chartData = [];";
    html += "for (var i = 0; i < pressureData.readings.length; i++) {";
    html += "  var reading = pressureData.readings[i];";
    html += "  chartData.push({";
    html += "    x: new Date(reading.time * 1000),";
    html += "    y: reading.pressure";
    html += "  });";
    html += "}";
    
    // Create chart
    html += "var chart = new Chart(";
    html += "  document.getElementById('chart-container').getContext('2d'),";
    html += "  {";
    html += "    type: 'line',";
    html += "    data: {";
    html += "      datasets: [{";
    html += "        label: 'Pressure (bar)',";
    html += "        data: chartData,";
    html += "        borderColor: 'rgb(75, 192, 192)',";
    html += "        tension: 0.1";
    html += "      }]";
    html += "    },";
    html += "    options: {";
    html += "      scales: {";
    html += "        x: {";
    html += "          type: 'time',";
    html += "          time: {";
    html += "            unit: 'hour'";
    html += "          },";
    html += "          title: {";
    html += "            display: true,";
    html += "            text: 'Time'";
    html += "          }";
    html += "        },";
    html += "        y: {";
    html += "          title: {";
    html += "            display: true,";
    html += "            text: 'Pressure (bar)'";
    html += "          }";
    html += "        }";
    html += "      }";
    html += "    }";
    html += "  }";
    html += ");";
    html += "</script>";
    
    return html;
}

bool PressureLogger::clearReadings() {
    // Clear readings
    readings.clear();
    lastRecordedPressure = 0;
    
    // Delete file
    if (LittleFS.exists(LOG_FILE)) {
        if (!LittleFS.remove(LOG_FILE)) {
            Serial.println("Failed to delete pressure log file");
            return false;
        }
    }
    
    return true;
}

void PressureLogger::trimOldReadings(size_t maxEntries) {
    if (readings.size() <= maxEntries) {
        return;
    }
    
    // Calculate how many entries to remove
    size_t entriesToRemove = readings.size() - maxEntries;
    
    // Remove oldest entries
    readings.erase(readings.begin(), readings.begin() + entriesToRemove);
    
    Serial.print("Trimmed ");
    Serial.print(entriesToRemove);
    Serial.println(" old pressure readings");
}

bool PressureLogger::checkSpaceAndTrim() {
    // Check if filesystem space is low
    if (checkFileSystemSpace()) {
        Serial.println("Low space detected, trimming pressure logs");
        
        // Trim pressure readings if they exist
        if (!readings.empty()) {
            // Keep only half of the readings
            size_t keepEntries = readings.size() / 2;
            if (keepEntries < 100) {
                keepEntries = 100; // Keep at least 100 entries
            }
            trimOldReadings(keepEntries);
            saveReadings();
        }
        
        return true;
    }
    
    return false;
}

bool PressureLogger::checkFileSystemSpace() {
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
