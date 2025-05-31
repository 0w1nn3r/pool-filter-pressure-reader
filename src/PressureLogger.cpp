#include "PressureLogger.h"

const char* PressureLogger::LOG_FILE = "/pressure_history.json";
const float PressureLogger::PRESSURE_CHANGE_THRESHOLD = 0.1f; // Record if pressure changes by 0.1 bar or more

PressureLogger::PressureLogger(TimeManager& tm, Settings& settings) 
    : timeManager(tm), settings(&settings), initialized(false), lastRecordedPressure(0), lastSaveTime(0) {
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

void PressureLogger::addReadingWithTimestamp(const PressureReading& reading) {
    // Check if initialized
    if (!initialized) {
        return;
    }
    
    // Add the reading with the provided timestamp
    readings.push_back(reading);
    lastRecordedPressure = reading.pressure;
    
    // Trim old readings if we exceed maximum
    if (readings.size() > MAX_READINGS) {
        trimOldReadings(MAX_READINGS);
    }
    
    // We don't save immediately here as the simulated data generation
    // will call saveReadings() explicitly after adding all readings
}

void PressureLogger::update() {
    // Check if we need to save readings
    if (initialized && !readings.empty()) {
        unsigned long currentTime = millis();
        if (currentTime - lastSaveTime >= saveInterval) {
            // Prune old data based on retention period
            pruneOldData();
            
            // Save readings to file
            saveReadings();
        }
    }
}

void PressureLogger::pruneOldData() {
    if (!initialized || readings.empty() || !settings) {
        return;
    }
    
    // Get current time
    time_t currentTime = timeManager.getCurrentTime();
    if (currentTime < 1609459200) { // Jan 1, 2021 timestamp
        // Invalid time, don't prune
        return;
    }
    
    // Get retention period in days
    unsigned int retentionDays = settings->getDataRetentionDays();
    
    // Calculate cutoff time (current time - retention days)
    time_t cutoffTime = currentTime - (retentionDays * 24 * 60 * 60);
    
    // Count readings to remove
    size_t removeCount = 0;
    for (const PressureReading& reading : readings) {
        if (reading.timestamp < cutoffTime) {
            removeCount++;
        } else {
            // Readings are assumed to be in chronological order
            break;
        }
    }
    
    // Remove old readings
    if (removeCount > 0) {
        readings.erase(readings.begin(), readings.begin() + removeCount);
        Serial.print("Pruned ");
        Serial.print(removeCount);
        Serial.println(" old readings based on retention period");
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
    
    // Add chart container with canvas element
    html += "<div style='width: 100%; height: 400px;'><canvas id='pressure-chart'></canvas></div>";
    
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
    
    // Sort data by timestamp
    html += "chartData.sort(function(a, b) { return a.x - b.x; });";
    
    // Create chart
    html += "document.addEventListener('DOMContentLoaded', function() {";
    html += "  var ctx = document.getElementById('pressure-chart').getContext('2d');";
    html += "  var chart = new Chart(ctx, {";
    html += "    type: 'line',";
    html += "    data: {";
    html += "      datasets: [{";
    html += "        label: 'Pressure (bar)',";
    html += "        data: chartData,";
    html += "        borderColor: 'rgb(75, 192, 192)',";
    html += "        backgroundColor: 'rgba(75, 192, 192, 0.2)',";
    html += "        borderWidth: 2,";
    html += "        pointRadius: 3,";
    html += "        pointHoverRadius: 5,";
    html += "        tension: 0.1";
    html += "      }]";
    html += "    },";
    html += "    options: {";
    html += "      responsive: true,";
    html += "      maintainAspectRatio: false,";
    html += "      scales: {";
    html += "        x: {";
    html += "          type: 'time',";
    html += "          time: {";
    html += "            unit: 'day',";
    html += "            displayFormats: {";
    html += "              day: 'MMM d'";
    html += "            }";
    html += "          },";
    html += "          title: {";
    html += "            display: true,";
    html += "            text: 'Date'";
    html += "          }";
    html += "        },";
    html += "        y: {";
    html += "          beginAtZero: false,";
    html += "          title: {";
    html += "            display: true,";
    html += "            text: 'Pressure (bar)'";
    html += "          }";
    html += "        }";
    html += "      },";
    html += "      plugins: {";
    html += "        legend: {";
    html += "          position: 'top'";
    html += "        },";
    html += "        tooltip: {";
    html += "          callbacks: {";
    html += "            title: function(tooltipItems) {";
    html += "              var date = new Date(tooltipItems[0].raw.x);";
    html += "              return date.toLocaleDateString() + ' ' + date.toLocaleTimeString();";
    html += "            }";
    html += "          }";
    html += "        }";
    html += "      }";
    html += "    }";
    html += "  });";
    html += "});";
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
