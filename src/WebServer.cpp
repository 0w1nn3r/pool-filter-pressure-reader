#include "WebServer.h"
#include <EEPROM.h>

extern "C" {
  #include "user_interface.h"
}

// Pressure sensor calibration
extern float PRESSURE_MAX;

WebServer::WebServer(float& pressure, float& threshold, unsigned int& duration, 
                     bool& active, unsigned long& startTime, bool& configChanged)
    : server(80), 
      currentPressure(pressure),
      backflushThreshold(threshold),
      backflushDuration(duration),
      backflushActive(active),
      backflushStartTime(startTime),
      backflushConfigChanged(configChanged) {
}

void WebServer::begin() {
    // Setup web server routes
    server.on("/", HTTP_GET, [this](){ this->handleRoot(); });
    server.on("/api", HTTP_GET, [this](){ this->handleAPI(); });
    server.on("/backflush", HTTP_POST, [this](){ this->handleBackflushConfig(); });
    server.begin();
    Serial.println("HTTP server started");
}

void WebServer::handleClient() {
    server.handleClient();
}

void WebServer::handleRoot() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "  <title>Pool Filter Pressure Monitor</title>\n";
  html += "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n";
  html += "  <meta http-equiv='refresh' content='5'>\n";
  html += "  <style>\n";
  html += "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }\n";
  html += "    .container { max-width: 600px; margin: 0 auto; }\n";
  html += "    .pressure-display { font-size: 48px; margin: 20px 0; }\n";
  html += "    .info { font-size: 14px; color: #666; margin-top: 40px; }\n";
  html += "    .gauge { width: 200px; height: 200px; margin: 20px auto; position: relative; }\n";
  html += "    .gauge-body { width: 100%; height: 100%; border-radius: 50%; background-color: #eee; }\n";
  html += "    .gauge-fill { position: absolute; top: 0; left: 0; width: 100%; height: 100%; border-radius: 50%; clip: rect(0, 200px, 200px, 100px); }\n";
  html += "    .gauge-cover { width: 70%; height: 70%; background-color: white; border-radius: 50%; position: absolute; top: 15%; left: 15%; display: flex; align-items: center; justify-content: center; font-size: 24px; }\n";
  html += "    .backflush-config { margin: 30px 0; padding: 20px; background-color: #f5f5f5; border-radius: 10px; }\n";
  html += "    .backflush-config h2 { margin-top: 0; }\n";
  html += "    .form-group { margin-bottom: 15px; }\n";
  html += "    label { display: inline-block; width: 120px; text-align: right; margin-right: 10px; }\n";
  html += "    input[type=number] { width: 80px; padding: 5px; }\n";
  html += "    button { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }\n";
  html += "    .status { margin-top: 10px; font-weight: bold; }\n";
  html += "    .active { color: #F44336; }\n";
  html += "  </style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "  <div class='container'>\n";
  html += "    <h1>Pool Filter Pressure Monitor</h1>\n";
  html += "    <div class='pressure-display'>" + String(currentPressure, 1) + " bar</div>\n";
  
  // Calculate gauge fill percentage and color
  float percentage = (currentPressure / PRESSURE_MAX) * 100;
  String gaugeColor = "#4CAF50"; // Green by default
  
  if (percentage > 75) {
    gaugeColor = "#F44336"; // Red for high pressure
  } else if (percentage > 50) {
    gaugeColor = "#FF9800"; // Orange for medium pressure
  }
  
  float rotation = (percentage / 100) * 180;
  
  html += "    <div class='gauge'>\n";
  html += "      <div class='gauge-body'></div>\n";
  html += "      <div class='gauge-fill' style='transform: rotate(" + String(rotation) + "deg); background-color: " + gaugeColor + ";'></div>\n";
  html += "      <div class='gauge-cover'>" + String(currentPressure, 1) + "</div>\n";
  html += "    </div>\n";
  html += "    <p>Last updated: " + String(millis() / 1000) + " seconds ago</p>\n";
  
  // Add backflush status
  html += "    <div class='status'>";
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    html += "<p class='active'>BACKFLUSH ACTIVE: " + String(elapsedTime) + "/" + String(backflushDuration) + " seconds</p>";
  } else {
    html += "<p>Backflush threshold: " + String(backflushThreshold, 1) + " bar</p>";
  }
  html += "</div>\n";
  
  // Add backflush configuration form
  html += "    <div class='backflush-config'>\n";
  html += "      <h2>Backflush Configuration</h2>\n";
  html += "      <form id='backflushForm'>\n";
  html += "        <div class='form-group'>\n";
  html += "          <label for='threshold'>Threshold (bar):</label>\n";
  html += "          <input type='number' id='threshold' name='threshold' min='0.5' max='" + String(PRESSURE_MAX) + "' step='0.1' value='" + String(backflushThreshold, 1) + "'>\n";
  html += "        </div>\n";
  html += "        <div class='form-group'>\n";
  html += "          <label for='duration'>Duration (sec):</label>\n";
  html += "          <input type='number' id='duration' name='duration' min='5' max='300' step='1' value='" + String(backflushDuration) + "'>\n";
  html += "        </div>\n";
  html += "        <button type='button' onclick='saveConfig()'>Save Configuration</button>\n";
  html += "        <p id='configStatus'></p>\n";
  html += "      </form>\n";
  html += "    </div>\n";
  
  // Add JavaScript for form submission
  html += "    <script>\n";
  html += "      function saveConfig() {\n";
  html += "        const threshold = document.getElementById('threshold').value;\n";
  html += "        const duration = document.getElementById('duration').value;\n";
  html += "        const status = document.getElementById('configStatus');\n";
  html += "        \n";
  html += "        fetch('/backflush', {\n";
  html += "          method: 'POST',\n";
  html += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n";
  html += "          body: 'threshold=' + threshold + '&duration=' + duration\n";
  html += "        })\n";
  html += "        .then(response => response.text())\n";
  html += "        .then(data => {\n";
  html += "          status.textContent = data;\n";
  html += "          status.style.color = 'green';\n";
  html += "          setTimeout(() => { status.textContent = ''; }, 3000);\n";
  html += "        })\n";
  html += "        .catch(error => {\n";
  html += "          status.textContent = 'Error: ' + error;\n";
  html += "          status.style.color = 'red';\n";
  html += "        });\n";
  html += "      }\n";
  html += "    </script>\n";
  
  html += "    <p class='info'>This page auto-refreshes every 5 seconds</p>\n";
  html += "    <p>API: <a href='/api'>/api</a> (JSON format)</p>\n";
  html += "  </div>\n";
  html += "</body>\n";
  html += "</html>";
  
  server.send(200, "text/html", html);
}

void WebServer::handleAPI() {
  String json = "{";
  json += "\"pressure\":" + String(currentPressure, 2) + ",";
  json += "\"unit\":\"bar\",";
  json += "\"timestamp\":" + String(millis() / 1000) + ",";
  json += "\"wifi_strength\":" + String(WiFi.RSSI()) + ",";
  json += "\"backflush_threshold\":" + String(backflushThreshold, 2) + ",";
  json += "\"backflush_duration\":" + String(backflushDuration) + ",";
  json += "\"backflush_active\":" + String(backflushActive ? "true" : "false");
  
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    json += ",\"backflush_elapsed\":" + String(elapsedTime);
  }
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void WebServer::handleBackflushConfig() {
  if (server.hasArg("threshold") && server.hasArg("duration")) {
    float newThreshold = server.arg("threshold").toFloat();
    unsigned int newDuration = server.arg("duration").toInt();
    
    // Validate values
    if (newThreshold >= 0.5 && newThreshold <= PRESSURE_MAX && 
        newDuration >= 5 && newDuration <= 300) {
      backflushThreshold = newThreshold;
      backflushDuration = newDuration;
      backflushConfigChanged = true;
      
      // Save to EEPROM is handled in main.cpp
      
      server.send(200, "text/plain", "Configuration updated");
    } else {
      server.send(400, "text/plain", "Invalid values");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}
