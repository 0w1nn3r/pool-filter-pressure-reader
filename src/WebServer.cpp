#include "WebServer.h"

extern "C" {
  #include "user_interface.h"
}

// Pressure sensor calibration
extern float PRESSURE_MAX;

// Implementation of the drawArcSegment function
String WebServer::drawArcSegment(float cx, float cy, float radius, float startAngle, float endAngle, String color, float opacity) {
  // Calculate start and end points of the arc
  float startX = cx + radius * cos(startAngle);
  float startY = cy + radius * sin(startAngle);
  float endX = cx + radius * cos(endAngle);
  float endY = cy + radius * sin(endAngle);
  
  // Determine if the arc is larger than 180 degrees (π radians)
  int largeArcFlag = (endAngle - startAngle > 3.14159) ? 1 : 0;
  
  // Create the SVG path for the arc
  String path = "        <path d='M " + String(cx) + "," + String(cy) + " L " + 
                String(startX) + "," + String(startY) + " A " + 
                String(radius) + " " + String(radius) + " 0 " + 
                String(largeArcFlag) + " 1 " + 
                String(endX) + "," + String(endY) + " Z' " + 
                "fill='" + color + "' fill-opacity='" + String(opacity) + "' />\n";
  
  return path;
}

WebServer::WebServer(float& pressure, float& threshold, unsigned int& duration, 
                     bool& active, unsigned long& startTime, bool& configChanged,
                     TimeManager& tm, BackflushLogger& logger, Settings& settings,
                     PressureLogger& pressureLog)
    : server(80), 
      currentPressure(pressure),
      backflushThreshold(threshold),
      backflushDuration(duration),
      backflushActive(active),
      backflushStartTime(startTime),
      backflushConfigChanged(configChanged),
      timeManager(tm),
      backflushLogger(logger),
      settings(settings),
      pressureLogger(pressureLog) {
}

void WebServer::begin() {
    // Setup web server routes
    server.on("/", [this]() { handleRoot(); });
    server.on("/api", [this]() { handleAPI(); });
    server.on("/backflush", [this]() { handleBackflushConfig(); });
    server.on("/log", [this]() { handleBackflushLog(); });
    server.on("/clearlog", [this]() { handleClearLog(); });
    server.on("/pressure", [this]() { handlePressureHistory(); });
    server.on("/clearpressure", [this]() { handleClearPressureHistory(); });
    server.on("/wifireset", [this]() { handleWiFiReset(); });
    server.on("/manualbackflush", HTTP_POST, std::bind(&WebServer::handleManualBackflush, this));
    server.on("/stopbackflush", HTTP_POST, std::bind(&WebServer::handleStopBackflush, this));
    server.on("/settings", [this]() { handleSettings(); });
    server.on("/sensorconfig", HTTP_POST, std::bind(&WebServer::handleSensorConfig, this));
    server.on("/setretention", HTTP_POST, std::bind(&WebServer::handleSetRetention, this));
    
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
  html += "    .gauge-container { width: 250px; height: 250px; margin: 20px auto; position: relative; }\n";
  html += "    .gauge-bg { fill: #f0f0f0; }\n";
  html += "    .gauge-dial { fill: none; stroke-width: 10; stroke-linecap: round; }\n";
  html += "    .gauge-value-text { font-family: Arial; font-size: 24px; font-weight: bold; text-anchor: middle; }\n";
  html += "    .gauge-label { font-family: Arial; font-size: 12px; text-anchor: middle; }\n";
  html += "    .gauge-tick { stroke: #333; stroke-width: 1; }\n";
  html += "    .gauge-tick-label { font-family: Arial; font-size: 10px; text-anchor: middle; }\n";
  html += "    .gauge-pointer { stroke: #cc0000; stroke-width: 4; stroke-linecap: round; }\n";
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
  
  // Calculate gauge rotation angle based on pressure
  float percentage = (currentPressure / PRESSURE_MAX) * 100;
  
  // Define the gauge sweep from 135 degrees (lower left) to 405 degrees (lower left + 270)
  // This creates a 270-degree sweep clockwise
  float startAngle = 135.0;
  float endAngle = 405.0;
  float angle = startAngle + (percentage / 100) * 270.0;
  
  // Calculate threshold percentages
  float thresholdPercentage = (backflushThreshold / PRESSURE_MAX) * 100;
  float thresholdPlusMarginPercentage = ((backflushThreshold + 0.2) / PRESSURE_MAX) * 100;
  
  // Calculate angles for the colored segments
  float thresholdAngle = startAngle + (thresholdPercentage / 100) * 270.0;
  float thresholdPlusMarginAngle = startAngle + (thresholdPlusMarginPercentage / 100) * 270.0;
  
  // Create SVG gauge
  html += "    <div class='gauge-container'>\n";
  html += "      <svg width='250' height='250' viewBox='0 0 250 250'>\n";
  
  // Background circle
  html += "        <circle cx='125' cy='125' r='120' class='gauge-bg' />\n";
  
  // Draw the colored arcs
  // Green segment (0 to threshold)
  float greenStartAngle = startAngle * (3.14159 / 180);
  float greenEndAngle = thresholdAngle * (3.14159 / 180);
  html += drawArcSegment(125, 125, 105, greenStartAngle, greenEndAngle, "#4CAF50", 0.2);
  
  // Orange segment (threshold to threshold+0.2)
  float orangeStartAngle = thresholdAngle * (3.14159 / 180);
  float orangeEndAngle = thresholdPlusMarginAngle * (3.14159 / 180);
  html += drawArcSegment(125, 125, 105, orangeStartAngle, orangeEndAngle, "#FF9800", 0.2);
  
  // Red segment (threshold+0.2 to max)
  float redStartAngle = thresholdPlusMarginAngle * (3.14159 / 180);
  float redEndAngle = endAngle * (3.14159 / 180);
  html += drawArcSegment(125, 125, 105, redStartAngle, redEndAngle, "#F44336", 0.2);
  
  // Draw tick marks and labels
  for (int i = 0; i <= 10; i++) {
    float tickAngle = startAngle + (i * 27); // 270 degrees / 10 = 27 degrees per tick
    float tickRadians = tickAngle * 3.14159 / 180;
    
    // Calculate tick positions
    float innerX = 125 + 90 * cos(tickRadians);
    float innerY = 125 + 90 * sin(tickRadians);
    float outerX = 125 + 105 * cos(tickRadians);
    float outerY = 125 + 105 * sin(tickRadians);
    
    // Draw tick line
    html += "        <line x1='" + String(innerX) + "' y1='" + String(innerY) + "' x2='" + String(outerX) + "' y2='" + String(outerY) + "' class='gauge-tick' />\n";
    
    // Draw tick label
    float labelX = 125 + 75 * cos(tickRadians);
    float labelY = 125 + 75 * sin(tickRadians);
    float tickValue = (i / 10.0) * PRESSURE_MAX;
    html += "        <text x='" + String(labelX) + "' y='" + String(labelY) + "' class='gauge-tick-label'>" + String(tickValue, 1) + "</text>\n";
  }
  
  // Draw the pointer
  float pointerRadians = angle * 3.14159 / 180;
  float pointerX = 125 + 90 * cos(pointerRadians);
  float pointerY = 125 + 90 * sin(pointerRadians);
  
  html += "        <line x1='125' y1='125' x2='" + String(pointerX) + "' y2='" + String(pointerY) + "' class='gauge-pointer' />\n";
  html += "        <circle cx='125' cy='125' r='10' fill='#333' />\n"; // Pointer pivot
  html += "      </svg>\n";
  html += "    </div>\n";
  html += "    <p>Last updated: " + String(millis() / 1000) + " seconds ago</p>\n";
  
  // Add backflush status and manual trigger button
  html += "    <div class='status'>";
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    html += "<p class='active'>BACKFLUSH ACTIVE: " + String(elapsedTime) + "/" + String(backflushDuration) + " seconds</p>";
    html += "<form method='POST' action='/stopbackflush' onsubmit='return confirm(\"Stop backflush now?\");'>";
    html += "<button type='submit' class='button' style='background-color: #f44336; margin-top: 10px;'>Stop Backflush</button>";
    html += "</form>";
  } else {
    html += "<p>Backflush threshold: " + String(backflushThreshold, 1) + " bar</p>";
    html += "<form method='POST' action='/manualbackflush' onsubmit='return confirm(\"Start backflush now?\");'>";
    html += "<button type='submit' class='button' style='background-color: #4CAF50; margin-top: 10px;'>Backflush Now</button>";
    html += "</form>";
  }
  html += "    </div>\n";
  
  // Add navigation links
  html += "    <div class='navigation'>\n";
  html += "      <p>\n";
  html += "        <a href='/log' style='margin-right: 15px;'>View Backflush Log</a>\n";
  html += "        <a href='/pressure' style='margin-right: 15px;'>View Pressure History</a>\n";
  html += "        <a href='/settings' style='margin-right: 15px;'>Sensor Settings</a>\n";
  html += "        <a href='/wifireset'>WiFi Settings</a>\n";
  html += "      </p>\n";
  html += "    </div>\n";
  
  // Add backflush configuration form
  html += "    <div class='backflush-config'>\n";
  html += "      <h2>Backflush Configuration</h2>\n";
  html += "      <form id='backflushForm'>\n";
  html += "        <div class='form-group'>\n";
  html += "          <label for='threshold'>Threshold (bar):</label>\n";
  html += "          <input type='number' id='threshold' name='threshold' min='0.3' max='" + String(PRESSURE_MAX) + "' step='0.1' value='" + String(backflushThreshold, 1) + "'>\n";
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
  
  // Add current time if available
  if (timeManager.isTimeInitialized()) {
    html += "    <p>Current time: " + timeManager.getFormattedDateTime() + " (GMT+1)</p>\n";
  }
  html += "  </div>\n";
  html += "</body>\n";
  html += "</html>";
  
  server.send(200, "text/html", html);
}

void WebServer::handleAPI() {
  String json = "{";
  json += "\"pressure\":" + String(currentPressure, 2) + ",";
  json += "\"unit\":\"bar\",";
  
  // Use NTP time if available, otherwise use uptime
  if (timeManager.isTimeInitialized()) {
    json += "\"timestamp\":" + String(timeManager.getCurrentTime()) + ",";
    json += "\"datetime\":\"" + timeManager.getFormattedDateTime() + "\",";
  } else {
    json += "\"timestamp\":" + String(millis() / 1000) + ",";
  }
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
    if (newThreshold >= 0.3 && newThreshold <= PRESSURE_MAX && 
        newDuration >= 3 && newDuration <= 300) {
      backflushThreshold = newThreshold;
      backflushDuration = newDuration;
      
      // Update settings
      settings.setBackflushThreshold(newThreshold);
      settings.setBackflushDuration(newDuration);
      
      backflushConfigChanged = true;
      
      server.send(200, "text/plain", "Configuration updated");
    } else {
      server.send(400, "text/plain", "Invalid values");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void WebServer::handleBackflushLog() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "  <title>Backflush Event Log</title>\n";
  html += "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n";
  html += "  <style>\n";
  html += "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "    .container { max-width: 800px; margin: 0 auto; }\n";
  html += "    h1 { color: #2c3e50; }\n";
  html += "    .events-table { width: 100%; border-collapse: collapse; margin: 20px 0; }\n";
  html += "    .events-table th, .events-table td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }\n";
  html += "    .events-table th { background-color: #f5f5f5; }\n";
  html += "    .events-table tr:hover { background-color: #f9f9f9; }\n";
  html += "    .button { display: inline-block; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; }\n";
  html += "    .button.danger { background-color: #e74c3c; }\n";
  html += "    .button:hover { opacity: 0.9; }\n";
  html += "  </style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "  <div class='container'>\n";
  html += "    <h1>Backflush Event Log</h1>\n";
  
  // Add current time if available
  if (timeManager.isTimeInitialized()) {
    html += "    <p>Current time: " + timeManager.getFormattedDateTime() + " (GMT+1)</p>\n";
  }
  
  // Add event count
  html += "    <p>Total events: " + String(backflushLogger.getEventCount()) + "</p>\n";
  
  // Add events table
  html += backflushLogger.getEventsAsHtml();
  
  // Add navigation and action buttons
  html += "    <p>\n";
  html += "      <a href='/' class='button'>Back to Dashboard</a>\n";
  html += "      <a href='/clearlog' class='button danger' onclick='return confirm(\"Are you sure you want to clear all log entries?\")'>Clear Log</a>\n";
  html += "    </p>\n";
  
  html += "  </div>\n";
  html += "</body>\n";
  html += "</html>";
  
  server.send(200, "text/html", html);
}

void WebServer::handleClearLog() {
    backflushLogger.clearEvents();
    server.sendHeader("Location", "/log");
    server.send(303); // Redirect back to log page
}

void WebServer::handlePressureHistory() {
    String html = "<!DOCTYPE html>\n";
    html += "<html>\n";
    html += "<head>\n";
    html += "<meta charset=\"UTF-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "<title>Pool Pressure History</title>\n";
    html += "<style>\n";
    html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; }\n";
    html += "h1, h2 { color: #0066cc; }\n";
    html += "a { color: #0066cc; text-decoration: none; }\n";
    html += "a:hover { text-decoration: underline; }\n";
    html += "button { background-color: #0066cc; color: white; border: none; padding: 8px 16px; cursor: pointer; }\n";
    html += "button:hover { background-color: #0052a3; }\n";
    html += "table { width: 100%; border-collapse: collapse; margin: 20px 0; }\n";
    html += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }\n";
    html += "th { background-color: #f2f2f2; }\n";
    html += "tr:nth-child(even) { background-color: #f9f9f9; }\n";
    html += "#chart-container { width: 100%; height: 300px; margin: 20px 0; }\n";
    html += "</style>\n";
    // Load Chart.js and required plugins for time scale and zooming
    html += "<script src=\"https://cdn.jsdelivr.net/npm/moment@2.29.1/min/moment.min.js\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js@2.9.4/dist/Chart.min.js\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chartjs-adapter-moment@0.1.2/dist/chartjs-adapter-moment.min.js\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/hammerjs@2.0.8/hammer.min.js\"></script>\n";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chartjs-plugin-zoom@0.7.7/dist/chartjs-plugin-zoom.min.js\"></script>\n";
    html += "</head>\n";
    html += "<body>\n";
    html += "<h1>Pool Pressure History</h1>\n";
    html += "<p>Current time: " + timeManager.getCurrentTimeStr() + " (GMT+1)</p>\n";
    
    // Add navigation links
    html += "<p><a href=\"/\">Back to Dashboard</a> | ";
    html += "<a href=\"/log\">View Backflush Log</a> | ";
    html += "<a href=\"/clearpressure\" onclick=\"return confirm('Are you sure you want to clear all pressure history?');\">Clear Pressure History</a></p>\n";
    
    // Add chart container with reset zoom button
    html += "<h2>Pressure History Chart</h2>\n";
    html += "<div style=\"margin-bottom: 10px;\">\n";
    html += "  <button id=\"reset-zoom\" style=\"padding: 5px 10px; background-color: #0066cc; color: white; border: none; border-radius: 4px; cursor: pointer;\">Reset Zoom</button>\n";
    html += "  <span style=\"margin-left: 10px; font-size: 0.9em; color: #666;\">Tip: Drag to zoom, double-click to reset</span>\n";
    html += "</div>\n";
    html += "<div id=\"chart-container\">\n";
    html += "  <canvas id=\"pressure-chart\"></canvas>\n";
    html += "</div>\n";
    
    // Get pressure data as JSON
    String jsonData = pressureLogger.getReadingsAsJson();
    
    // Add current device time to JSON
    jsonData.remove(jsonData.length() - 1); // Remove the closing brace
    jsonData += ",\"currentTime\":" + String(timeManager.getCurrentTime()) + "}";
    
    // Add JavaScript for chart
    html += "<script>\n";
    html += "  // Parse pressure data\n";
    html += "  var pressureData = " + jsonData + ";\n";
    
    html += "  // Check if we have data\n";
    html += "  if (!pressureData || !pressureData.readings || pressureData.readings.length === 0) {\n";
    html += "    document.getElementById('chart-container').innerHTML = '<p>No pressure readings recorded yet.</p>';\n";
    html += "  } else {\n";
    html += "    // Prepare data for Chart.js\n";
    html += "    var chartData = [];\n";
    
    html += "    // Sort data by timestamp\n";
    html += "    pressureData.readings.sort(function(a, b) { return a.time - b.time; });\n";
    
    html += "    // Use device time for filtering\n";
    html += "    var deviceTime = pressureData.currentTime;\n";
    html += "    // Process each reading, filtering out future timestamps\n";
    html += "    for (var i = 0; i < pressureData.readings.length; i++) {\n";
    html += "      var reading = pressureData.readings[i];\n";
    html += "      // Skip readings with future timestamps\n";
    html += "      if (reading.time > deviceTime) {\n";
    html += "        console.log('Skipping future timestamp: ' + new Date(reading.time * 1000));\n";
    html += "        continue;\n";
    html += "      }\n";
    html += "      // Convert UTC timestamp to local time by subtracting 1 hour\n";
    html += "      // This is needed because device is GMT+1 but browser is GMT+2\n";
    html += "      var localTime = new Date((reading.time - 3600) * 1000);\n";
    html += "      chartData.push({\n";
    html += "        x: localTime,\n";
    html += "        y: reading.pressure\n";
    html += "      });\n";
    html += "    }\n";
    
    html += "    // Create the chart\n";
    html += "    var ctx = document.getElementById('pressure-chart').getContext('2d');\n";
    html += "    var chart; // Define chart variable in wider scope for reset button access\n";
    html += "    chart = new Chart(ctx, {\n";
    html += "      type: 'line',\n";
    html += "      data: {\n";
    html += "        datasets: [{\n";
    html += "          label: 'Pressure (bar)',\n";
    html += "          data: chartData,\n";
    html += "          backgroundColor: 'rgba(75, 192, 192, 0.2)',\n";
    html += "          borderColor: 'rgb(75, 192, 192)',\n";
    html += "          tension: 0,\n";
    html += "          fill: false\n";
    html += "        }]\n";
    html += "      },\n";
    html += "      options: {\n";
    html += "        responsive: true,\n";
    html += "        maintainAspectRatio: false,\n";
    html += "        scales: {\n";
    html += "          xAxes: [{\n";
    html += "            type: 'time',\n";
    html += "            time: {\n";
    html += "              displayFormats: {\n";
    html += "                hour: 'MMM D, HH:mm'\n";
    html += "              },\n";
    html += "              timezone: 'Europe/Paris'\n";
    html += "            },\n";
    html += "            ticks: {\n";
    html += "              maxRotation: 45,\n";
    html += "              minRotation: 45,\n";
    html += "              autoSkip: true,\n";
    html += "              maxTicksLimit: 10\n";
    html += "            }\n";
    html += "          }],\n";
    html += "          yAxes: [{\n";
    html += "            ticks: {\n";
    html += "              beginAtZero: false\n";
    html += "            }\n";
    html += "          }]\n";
    html += "        },\n";
    html += "        plugins: {\n";
    html += "          zoom: {\n";
    html += "            pan: {\n";
    html += "              enabled: false\n";
    html += "            },\n";
    html += "            zoom: {\n";
    html += "              enabled: true,\n";
    html += "              mode: 'xy',\n";
    html += "              drag: true,\n";
    html += "              speed: 0.1,\n";
    html += "              threshold: 2,\n";
    html += "              sensitivity: 3\n";
    html += "            }\n";
    html += "          }\n";
    html += "        }\n";
    html += "      }\n";
    html += "    });\n";
    html += "    // Add reset zoom button functionality\n";
    html += "    document.getElementById('reset-zoom').addEventListener('click', function() {\n";
    html += "      chart.resetZoom();\n";
    html += "    });\n";
    html += "    // Also reset zoom on double-click\n";
    html += "    document.getElementById('pressure-chart').addEventListener('dblclick', function() {\n";
    html += "      chart.resetZoom();\n";
    html += "    });\n";
    html += "  }\n";
    html += "</script>\n";
    
    // Add summary information
    html += "<script>\n";
    html += "  // Display summary information if we have data\n";
    html += "  if (pressureData && pressureData.readings && pressureData.readings.length > 0) {\n";
    html += "    // Filter out future timestamps for display\n";
    html += "    var currentTime = Math.floor(Date.now() / 1000);\n";
    html += "    var validReadings = pressureData.readings.filter(function(reading) {\n";
    html += "      return reading.time <= currentTime;\n";
    html += "    });\n";
    html += "    document.write('<p><strong>Total readings:</strong> ' + validReadings.length + ' (valid) / ' + pressureData.readings.length + ' (total)</p>');\n";
    html += "    if (validReadings.length > 0) {\n";
    html += "      var firstDate = new Date(validReadings[0].time * 1000);\n";
    html += "      var lastDate = new Date(validReadings[validReadings.length-1].time * 1000);\n";
    html += "    } else {\n";
    html += "      document.write('<p><strong>Warning:</strong> No valid readings with timestamps in the past or present</p>');\n";
    html += "      return;\n";
    html += "    }\n";
    html += "    document.write('<p><strong>Date range:</strong> ' + firstDate.toLocaleString() + ' to ' + lastDate.toLocaleString() + '</p>');\n";
    html += "  }\n";
    html += "</script>\n";
    
    // Add data retention configuration form
    html += "<div style=\"margin-top: 30px; padding: 15px; background-color: #f5f5f5; border-radius: 5px;\">\n";
    html += "  <h3>Data Retention Settings</h3>\n";
    html += "  <form id=\"retentionForm\">\n";
    html += "    <label for=\"retentionDays\">Keep pressure data for: </label>\n";
    html += "    <input type=\"number\" id=\"retentionDays\" name=\"retentionDays\" min=\"1\" max=\"90\" value=\"" + String(settings.getDataRetentionDays()) + "\" style=\"width: 60px;\"> days\n";
    html += "    <button type=\"button\" onclick=\"saveRetentionSettings()\" style=\"margin-left: 10px;\">Save</button>\n";
    html += "    <p><small>Data older than this will be automatically pruned. Valid range: 1-90 days.</small></p>\n";
    html += "    <p id=\"retentionStatus\" style=\"font-weight: bold;\"></p>\n";
    html += "  </form>\n";
    html += "</div>\n";
    
    // Add JavaScript for retention form submission
    html += "<script>\n";
    html += "  function saveRetentionSettings() {\n";
    html += "    const retentionDays = document.getElementById('retentionDays').value;\n";
    html += "    const status = document.getElementById('retentionStatus');\n";
    html += "    \n";
    html += "    fetch('/setretention', {\n";
    html += "      method: 'POST',\n";
    html += "      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n";
    html += "      body: 'retentionDays=' + retentionDays\n";
    html += "    })\n";
    html += "    .then(response => response.json())\n";
    html += "    .then(data => {\n";
    html += "      status.textContent = data.message;\n";
    html += "      status.style.color = data.success ? 'green' : 'red';\n";
    html += "      setTimeout(() => { status.textContent = ''; }, 3000);\n";
    html += "    })\n";
    html += "    .catch(error => {\n";
    html += "      status.textContent = 'Error: ' + error;\n";
    html += "      status.style.color = 'red';\n";
    html += "    });\n";
    html += "  }\n";
    html += "</script>\n";
    
    html += "</body>\n";
    html += "</html>\n";
    
    server.send(200, "text/html", html);
}

void WebServer::handleClearPressureHistory() {
    pressureLogger.clearReadings();
    server.sendHeader("Location", "/pressure");
    server.send(303); // Redirect back to pressure history page
}

void WebServer::handleWiFiReset() {
    String html = "<!DOCTYPE html>\n";
    html += "<html>\n";
    html += "<head>\n";
    html += "  <title>WiFi Settings</title>\n";
    html += "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n";
    html += "  <style>\n";
    html += "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }\n";
    html += "    .container { max-width: 600px; margin: 0 auto; }\n";
    html += "    h1 { color: #2c3e50; }\n";
    html += "    .info { margin: 20px 0; padding: 15px; background-color: #f8f9fa; border-radius: 5px; }\n";
    html += "    .button { display: inline-block; padding: 12px 24px; background-color: #e74c3c; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; font-weight: bold; }\n";
    html += "    .button:hover { background-color: #c0392b; }\n";
    html += "    .back-link { display: block; margin-top: 30px; color: #3498db; }\n";
    html += "  </style>\n";
    html += "</head>\n";
    html += "<body>\n";
    html += "  <div class='container'>\n";
    html += "    <h1>WiFi Settings</h1>\n";
    
    html += "    <div class='info'>\n";
    html += "      <p>Current WiFi Network: <strong>" + WiFi.SSID() + "</strong></p>\n";
    html += "      <p>IP Address: " + WiFi.localIP().toString() + "</p>\n";
    html += "      <p>Signal Strength: " + String(WiFi.RSSI()) + " dBm</p>\n";
    html += "    </div>\n";
    
    html += "    <div class='info'>\n";
    html += "      <p>To change WiFi settings, click the button below.</p>\n";
    html += "      <p>The device will restart in configuration mode, creating a WiFi access point named <strong>PoolFilterAP</strong>.</p>\n";
    html += "      <p>Connect to this network and navigate to <strong>192.168.4.1</strong> to configure your new WiFi settings.</p>\n";
    html += "    </div>\n";
    
    // Add confirmation form with POST method for security
    html += "    <form method='POST' onsubmit='return confirm(\"Are you sure you want to reset WiFi settings? The device will restart.\");'>\n";
    html += "      <button type='submit' name='reset' value='true' class='button'>Reset WiFi Settings</button>\n";
    html += "    </form>\n";
    
    html += "    <a href='/' class='back-link'>Back to Home</a>\n";
    html += "  </div>\n";
    html += "</body>\n";
    html += "</html>\n";
    
    // Check if this is a POST request to reset WiFi
    if (server.method() == HTTP_POST && server.hasArg("reset")) {
        server.send(200, "text/html", "<html><body><h1>Resetting WiFi settings...</h1><p>The device will restart in configuration mode.</p></body></html>");
        delay(1000);
        // Reset WiFi settings and restart
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
    } else {
        server.send(200, "text/html", html);
    }
}

void WebServer::handleManualBackflush() {
    // Only process POST requests for security
    if (server.method() != HTTP_POST) {
        server.sendHeader("Location", "/");
        server.send(303); // Redirect to main page
        return;
    }
    
    // Don't start a new backflush if one is already active
    if (backflushActive) {
        server.send(200, "text/html", "<html><body><h1>Backflush Already Active</h1><p>A backflush operation is already in progress.</p><p><a href='/'>Return to Dashboard</a></p></body></html>");
        return;
    }
    
    // Start a backflush operation
    backflushActive = true;
    backflushStartTime = millis();
    
    // Log the manual backflush event with the current pressure
    backflushLogger.logEvent(currentPressure, backflushDuration, "Manual");
    
    Serial.print("Manual backflush started at pressure: ");
    Serial.print(currentPressure, 1);
    Serial.println(" bar");
    
    // Redirect back to the main page
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServer::handleStopBackflush() {
    if (!backflushActive) {
        server.send(400, "text/plain", "No backflush active");
        return;
    }
    
    // Calculate actual duration
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    
    // Deactivate backflush
    backflushActive = false;
    
    // Turn off relay and LED
    digitalWrite(RELAY_PIN, LOW);  // Deactivate relay
    digitalWrite(LED_PIN, HIGH);   // Turn LED OFF (inverse logic on NodeMCU)
    Serial.println("Manual backflush stopped");
    
    // Log the stopped backflush with actual duration
    String eventType = "Manual-Stopped";
    backflushLogger.logEvent(currentPressure, elapsedTime, eventType);
    
    Serial.println("Backflush stopped manually");
    Serial.print("Actual duration: ");
    Serial.print(elapsedTime);
    Serial.println(" seconds");
    
    // Redirect back to main page
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Redirecting to main page");
}

void WebServer::handleSettings() {
  String html = "<!DOCTYPE html>\n";
  html += "<html>\n";
  html += "<head>\n";
  html += "  <title>Sensor Settings</title>\n";
  html += "  <meta name='viewport' content='width=device-width, initial-scale=1'>\n";
  html += "  <style>\n";
  html += "    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }\n";
  html += "    .container { max-width: 800px; margin: 0 auto; }\n";
  html += "    h1 { color: #2c3e50; }\n";
  html += "    .settings-form { margin: 30px 0; padding: 20px; background-color: #f5f5f5; border-radius: 10px; }\n";
  html += "    .form-group { margin-bottom: 15px; }\n";
  html += "    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; }\n";
  html += "    input[type=number] { width: 80px; padding: 5px; }\n";
  html += "    button { background-color: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }\n";
  html += "    .button { display: inline-block; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; }\n";
  html += "    .button:hover { opacity: 0.9; }\n";
  html += "    .status { margin-top: 10px; font-weight: bold; }\n";
  html += "  </style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "  <div class='container'>\n";
  html += "    <h1>Sensor Settings</h1>\n";
  
  // Add current time if available
  if (timeManager.isTimeInitialized()) {
    html += "    <p>Current time: " + timeManager.getFormattedDateTime() + " (GMT+1)</p>\n";
  }
  
  // Sensor configuration form
  html += "    <div class='settings-form'>\n";
  html += "      <h2>Pressure Sensor Configuration</h2>\n";
  html += "      <p>Configure your pressure sensor by setting its maximum pressure range.</p>\n";
  html += "      <form id='sensorForm'>\n";
  html += "        <div class='form-group'>\n";
  html += "          <label for='sensormax'>Maximum Pressure (bar):</label>\n";
  html += "          <input type='number' id='sensormax' name='sensormax' min='1.0' max='10.0' step='0.5' value='" + String(PRESSURE_MAX, 1) + "'>\n";
  html += "          <p><small>Common values: 4.0 bar, 6.0 bar, 10.0 bar depending on your sensor type</small></p>\n";
  html += "        </div>\n";
  html += "        <button type='button' onclick='saveSensorConfig()'>Save Configuration</button>\n";
  html += "        <p id='configStatus'></p>\n";
  html += "      </form>\n";
  html += "    </div>\n";
  
  // Add navigation links
  html += "    <p><a href='/' class='button'>Back to Home</a></p>\n";
  
  // Add JavaScript for form submission
  html += "    <script>\n";
  html += "      function saveSensorConfig() {\n";
  html += "        const sensormax = document.getElementById('sensormax').value;\n";
  html += "        const status = document.getElementById('configStatus');\n";
  html += "        \n";
  html += "        fetch('/sensorconfig', {\n";
  html += "          method: 'POST',\n";
  html += "          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n";
  html += "          body: 'sensormax=' + sensormax\n";
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
  
  html += "  </div>\n";
  html += "</body>\n";
  html += "</html>";
  
  server.send(200, "text/html", html);
}
void WebServer::handleSensorConfig() {
    bool success = false;
    String message = "Failed to update sensor settings";
    
    if (server.hasArg("sensormax")) {
        String sensorMaxStr = server.arg("sensormax");
        float sensorMax = sensorMaxStr.toFloat();
        
        // Validate input
        if (sensorMax >= 1.0 && sensorMax <= 10.0) {
            // Update the setting
            settings.setSensorMaxPressure(sensorMax);
            
            // Update the global variable
            PRESSURE_MAX = sensorMax;
            
            success = true;
            message = "Sensor max pressure updated to: " + String(sensorMax, 1) + " bar";
            
            Serial.print("Sensor max pressure updated to: ");
            Serial.println(sensorMax);
        } else {
            message = "Invalid sensor max pressure value. Must be between 1.0 and 10.0 bar.";
        }
    }
    
    // Return JSON response instead of redirecting
    String jsonResponse = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    server.send(200, "application/json", jsonResponse);
}

void WebServer::handleSetRetention() {
    bool success = false;
    String message = "Failed to update retention settings";
    
    if (server.hasArg("retentionDays")) {
        String retentionDaysStr = server.arg("retentionDays");
        unsigned int retentionDays = retentionDaysStr.toInt();
        
        // Validate input
        if (retentionDays >= 1 && retentionDays <= 90) {
            // Update the setting
            settings.setDataRetentionDays(retentionDays);
            
            // Immediately prune old data based on new retention period
            pressureLogger.pruneOldData();
            pressureLogger.saveReadings();
            
            success = true;
            message = "Data retention period updated to: " + String(retentionDays) + " days";
            
            Serial.print("Data retention period updated to: ");
            Serial.print(retentionDays);
            Serial.println(" days");
        } else {
            message = "Invalid retention period. Must be between 1 and 90 days.";
        }
    }
    
    // Return JSON response instead of redirecting
    String jsonResponse = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    server.send(200, "application/json", jsonResponse);
}
