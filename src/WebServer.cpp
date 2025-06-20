#include "WebServer.h"
#include "version.h"

extern "C" {
  #include "user_interface.h"
#include <StreamString.h>
#include <functional>
}

#include <set> // For std::set to track unique SSIDs

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

WebServer::WebServer(float& pressure, int& rawADC, float& voltage, float& threshold, unsigned int& duration, 
                     bool& active, unsigned long& startTime, bool& configChanged,
                     String& backflushType, TimeManager& tm, BackflushLogger& logger, Settings& settings,
                     PressureLogger& pressureLog, BackflushScheduler& sched)
    : server(80), 
      currentPressure(pressure),
      rawADCValue(rawADC),
      sensorVoltage(voltage),
      backflushThreshold(threshold),
      backflushDuration(duration),
      backflushActive(active),
      backflushStartTime(startTime),
      backflushConfigChanged(configChanged),
      currentBackflushType(backflushType),
      timeManager(tm),
      backflushLogger(logger),
      settings(settings),
      scheduler(sched),
      otaEnabledTime(0),
      otaEnabled(false),
      pressureLogger(pressureLog),
      display(nullptr) {
}

void WebServer::setupOTA() {
    // Configure OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPort(8266); // Explicitly set the default port
    ArduinoOTA.setPassword(NULL); // No password protection
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    
    // Initialize OTA service
    ArduinoOTA.begin();
    
    Serial.println("OTA service initialized");
    Serial.print("Device hostname: ");
    Serial.println(HOSTNAME);
}

void WebServer::begin() {
    setupOTA();
    
    // Setup web server routes
    server.on("/", [this]() { handleRoot(); });
    server.on("/api", [this]() { handleAPI(); });
    server.on("/style.css", [this]() { handleCSS(); });
    server.on("/backflush", [this]() { handleBackflushConfig(); });
    server.on("/log", [this]() { handleBackflushLog(); });
    server.on("/clearlog", [this]() { handleClearLog(); });
    server.on("/pressure", [this]() { handlePressureHistory(); });
    server.on("/clearpressure", [this]() { handleClearPressureHistory(); });
    server.on("/wifi", HTTP_ANY, [this]() { handleWiFiConfigPage(); });
    server.on("/manualbackflush", HTTP_POST, std::bind(&WebServer::handleManualBackflush, this));
    server.on("/stopbackflush", HTTP_POST, std::bind(&WebServer::handleStopBackflush, this));
    server.on("/settings", [this]() { handleSettings(); });
    server.on("/schedule", [this]() { handleSchedulePage(); });
    server.on("/scheduleupdate", HTTP_POST, std::bind(&WebServer::handleScheduleUpdate, this));
    server.on("/scheduledelete", HTTP_POST, std::bind(&WebServer::handleScheduleDelete, this));
    server.on("/ota", HTTP_POST, [this]() { handleOTAUpdate(); });
    server.on("/otaupload", HTTP_GET, [this]() { handleOTAUploadPage(); });
    server.on("/otaupload", HTTP_POST, 
        [this](){ server.send(200, "text/plain", ""); }, 
        [this](){ handleOTAUpload(); }
    );
    server.on("/sensorconfig", HTTP_POST, std::bind(&WebServer::handleSensorConfig, this));
    server.on("/resetcalibration", HTTP_POST, std::bind(&WebServer::handleResetCalibration, this));
    server.on("/setretention", HTTP_POST, std::bind(&WebServer::handleSetRetention, this));
    server.on("/setpressurethreshold", HTTP_POST, std::bind(&WebServer::handleSetPressureThreshold, this));
    server.on("/setpressuremaxinterval", HTTP_POST, std::bind(&WebServer::handleSetPressureMaxInterval, this));
    server.on("/pressure.csv", [this]() { handlePressureCsv(); });
    server.on("/api/pressure/readings", HTTP_GET, [this]() { handlePressureReadingsApi(); });
    
    server.begin();
    Serial.println("HTTP server started");
}

void WebServer::handleClient() {
    server.handleClient();
    
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Check if OTA timeout has occurred
    if (otaEnabled && (millis() - otaEnabledTime > OTA_TIMEOUT)) {
        Serial.println("OTA update period expired");
        otaEnabled = false;
    }
}

void WebServer::handleOTAUpdate() {
    // Stop any existing OTA service
    ArduinoOTA.end();
    
    // Re-initialize OTA with explicit port
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(NULL);
    ArduinoOTA.setPort(8266); // Explicitly set the default port
   
    // Set the time when OTA was enabled
    otaEnabledTime = millis();
    otaEnabled = true;
    
    // Restart OTA service
    ArduinoOTA.begin();
    
    Serial.println("OTA updates enabled for 5 minutes on port 8266");
    Serial.print("Device hostname: ");
    Serial.println(HOSTNAME);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    String html = F(R"HTML(
OTA Update Mode Enabled
OTA updates enabled for 5 minutes.
You can now upload firmware using the Arduino IDE or PlatformIO.
Or use the web uploader:
)HTML");
    
    server.send(200, "text/html", html);
}

void WebServer::handleCSS() {
  String css = F(R"CSS(
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; color: #333; }
    .container { max-width: 600px; margin: 0 auto; }
    .pressure-display { font-size: 48px; margin: 20px 0; }
    .info { font-size: 14px; color: #666; margin-top: 40px; }
    .gauge-container { width: 250px; height: 250px; margin: 20px auto; position: relative; }
    .gauge-bg { fill: #f0f0f0; }
    .gauge-dial { fill: none; stroke-width: 10; stroke-linecap: round; }
    .gauge-value-text { font-family: Arial; font-size: 24px; font-weight: bold; text-anchor: middle; }
    .gauge-label { font-family: Arial; font-size: 12px; text-anchor: middle; }
    .gauge-tick { stroke: #333; stroke-width: 1; }
    .gauge-tick-label { font-family: Arial; font-size: 10px; text-anchor: middle; }
    .gauge-pointer { stroke: #cc0000; stroke-width: 4; stroke-linecap: round; }
    .backflush-config { margin: 30px 0; padding: 20px; background-color: #f5f5f5; border-radius: 10px; }
    .backflush-config h2 { margin-top: 0; }
    .form-group { margin-bottom: 15px; }
    label { display: inline-block; width: 120px; text-align: right; margin-right: 10px; }
    input[type=number] { width: 80px; padding: 5px; }
    button { background-color: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }
    button:hover { background-color: #45a049; }
    .status { margin-top: 10px; font-weight: bold; }
    .active { color: #F44336; }
    .navigation { margin: 20px 0; }
    .navigation a { margin-right: 15px; }
    h1, h2 { color: #0066cc; }
    a { color: #0066cc; text-decoration: none; }
    a:hover { text-decoration: underline; }
    table { width: 100%; border-collapse: collapse; margin: 20px 0; }
    th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }
    th { background-color: #f2f2f2; }
    tr:nth-child(even) { background-color: #f9f9f9; }
  )CSS");
  server.send(200, "text/css", css);
}

void WebServer::handleRoot() {
  Serial.println("Client connected: " + server.client().remoteIP().toString());
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  String html = F(R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Pool Filter Pressure Monitor</title>
  <link rel="stylesheet" href="/style.css">
</head>)HTML");
server.send(200, "text/html", html);
  
  // Add javascript
  html = F(R"HTML(<script>
    function saveConfig() {
      const threshold = document.getElementById('threshold').value;
      const duration = document.getElementById('duration').value;
      const status = document.getElementById('configStatus');
      
      fetch('/backflush', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'threshold=' + threshold + '&duration=' + duration
      })
      .then(response => response.text())
      .then(data => {
        status.textContent = data;
        status.style.color = 'green';
        setTimeout(() => { status.textContent = ''; }, 3000);
      })
      .catch(error => {
        status.textContent = 'Error: ' + error;
        status.style.color = 'red';
      });
})HTML");
server.sendContent(html);
html = F(R"HTML(
  function updateTimeDisplay() {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function() {
      if (xhr.readyState == 4 && xhr.status == 200) {
        var data = JSON.parse(xhr.responseText);
        var pressure = data.pressure;
        var pressureElement = document.getElementById('pressure-display');
        if (pressureElement) pressureElement.textContent = pressure.toFixed(1) + ' bar';
        // Update gauge needle position
        var needle = document.getElementById('gauge-needle');
        if (needle) {
          var startAngle = -225; // -225 degrees
          var endAngle = 45;     // 45 degrees 
          )HTML");
server.sendContent(html);
server.sendContent("var maxPressure = " + String(PRESSURE_MAX));
html = F(R"HTML(
          var percentage = (pressure / maxPressure);
          var angle = startAngle + (percentage * (endAngle - startAngle));
          var pointerRadians = angle * Math.PI / 180;
          var pointerX = 125 + 90 * Math.cos(pointerRadians);
          var pointerY = 125 + 90 * Math.sin(pointerRadians);
          needle.setAttribute('x2', pointerX);
          needle.setAttribute('y2', pointerY);
        }
        // Update current time if available
        if (data.datetime) {
          var timeElement = document.getElementById('current-time');
          if (timeElement) timeElement.textContent = data.datetime;
        }
        // Update uptime
        var uptimeElement = document.getElementById('uptime');
        if (uptimeElement && data.uptime) {
          var seconds = data.uptime;
          var days = Math.floor(seconds / 86400);
          seconds %= 86400;
          var hours = Math.floor(seconds / 3600);
          seconds %= 3600;
          var minutes = Math.floor(seconds / 60);
          seconds %= 60;
          var uptimeStr = '';
          if (days > 0) uptimeStr += days + 'd ';
          if (hours > 0 || days > 0) uptimeStr += hours + 'h ';
          if (minutes > 0 || hours > 0 || days > 0) uptimeStr += minutes + 'm ';
          uptimeStr += seconds + 's';
          uptimeElement.textContent = uptimeStr;
        }
)HTML");
        server.sendContent(html);
        html = F(R"HTML(
          // Update backflush threshold
          var thresholdElement = document.getElementById('backflush-threshold');
          if (thresholdElement && data.backflush_threshold) {
            var newThreshold = parseFloat(data.backflush_threshold);
            thresholdElement.textContent = newThreshold.toFixed(1);
            // Update the colored arcs when threshold changes
            updateGaugeArcs(newThreshold);
          }
          // Update backflush sections visibility based on active state
          var activeSection = document.getElementById('backflush-active-section');
          var inactiveSection = document.getElementById('backflush-inactive-section');
          if (activeSection && inactiveSection) {
            if (data.backflush_active === true) {
              activeSection.style.display = 'block';
            inactiveSection.style.display = 'none';
            // Update the status text
            var statusElement = document.getElementById('backflush-status');
            if (statusElement && data.backflush_elapsed !== undefined) {
              statusElement.textContent = data.backflush_elapsed + '/' + data.backflush_duration + ' seconds';
            }
          } else {
            activeSection.style.display = 'none';
            inactiveSection.style.display = 'block';
          }
        }
      }
    };
    xhr.open('GET', '/api', true);
    xhr.send();
  }

// Function to update the colored arcs based on threshold
function updateGaugeArcs(threshold) {
  var maxPressure = parseFloat(document.getElementById('max-pressure-value').textContent);
  var startAngle = 135.0;
  var endAngle = 405.0;
  
  // Calculate threshold percentages
  var thresholdPercentage = (threshold / maxPressure) * 100;
  var thresholdPlusMarginPercentage = ((threshold + 0.2) / maxPressure) * 100;
  
  // Calculate angles for the colored segments
  var thresholdAngle = startAngle + (thresholdPercentage / 100) * 270.0;
  var thresholdPlusMarginAngle = startAngle + (thresholdPlusMarginPercentage / 100) * 270.0;
  
  // Convert to radians for SVG path calculations
  var greenStartAngle = startAngle * (Math.PI / 180);
  var greenEndAngle = thresholdAngle * (Math.PI / 180);
  var orangeStartAngle = thresholdAngle * (Math.PI / 180);
  var orangeEndAngle = thresholdPlusMarginAngle * (Math.PI / 180);
  var redStartAngle = thresholdPlusMarginAngle * (Math.PI / 180);
  var redEndAngle = endAngle * (Math.PI / 180);
  
  // Update the SVG paths
  updateArcSegment('green-arc', 125, 125, 105, greenStartAngle, greenEndAngle, '#4CAF50', 0.2);
  updateArcSegment('orange-arc', 125, 125, 105, orangeStartAngle, orangeEndAngle, '#FF9800', 0.2);
  updateArcSegment('red-arc', 125, 125, 105, redStartAngle, redEndAngle, '#F44336', 0.2);
}

// Helper function to update an arc segment in the SVG
function updateArcSegment(id, cx, cy, radius, startAngle, endAngle, color, opacity) {
  // Calculate start and end points of the arc
  var startX = cx + radius * Math.cos(startAngle);
  var startY = cy + radius * Math.sin(startAngle);
  var endX = cx + radius * Math.cos(endAngle);
  var endY = cy + radius * Math.sin(endAngle);
  
  // Determine if the arc is larger than 180 degrees (π radians)
  var largeArcFlag = (endAngle - startAngle > Math.PI) ? 1 : 0;
  
  // Create the SVG path for the arc
  var path = 'M ' + cx + ',' + cy + ' L ' + 
            startX + ',' + startY + ' A ' + 
            radius + ' ' + radius + ' 0 ' + 
            largeArcFlag + ' 1 ' + 
            endX + ',' + endY + ' Z';
  
  // Update the existing path element
  var arcElement = document.getElementById(id);
  if (arcElement) {
    arcElement.setAttribute('d', path);
  }
}

  // Update time display every 1 second
  window.onload = function() {
    updateTimeDisplay();
    setInterval(updateTimeDisplay, 1000);
  };
</script>)HTML");
server.sendContent(html);

html = R"HTML(<body>
  <div class='container'>
    <h1>Pool Filter Pressure Monitor</h1>
    <div><span id='pressure-display' class='pressure-display'>)HTML";
  server.sendContent(html+String(currentPressure, 1) + "</span></div>");

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
  html = "    <div class='gauge-container'>\n";
  html += "      <svg width='250' height='250' viewBox='0 0 250 250'>\n";
  // Background circle
  html += "        <circle cx='125' cy='125' r='120' class='gauge-bg' />\n";
  // Draw the colored arcs
  // Green segment (0 to threshold)
  float greenStartAngle = startAngle * (3.14159 / 180);
  float greenEndAngle = thresholdAngle * (3.14159 / 180);
  String greenArc = drawArcSegment(125, 125, 105, greenStartAngle, greenEndAngle, "#4CAF50", 0.2);
  html += greenArc.substring(0, 14) + "id='green-arc' " + greenArc.substring(14);

  // Orange segment (threshold to threshold+0.2)
  float orangeStartAngle = thresholdAngle * (3.14159 / 180);
  float orangeEndAngle = thresholdPlusMarginAngle * (3.14159 / 180);
  String orangeArc = drawArcSegment(125, 125, 105, orangeStartAngle, orangeEndAngle, "#FF9800", 0.2);
  html += orangeArc.substring(0, 14) + "id='orange-arc' " + orangeArc.substring(14);

  // Red segment (threshold+0.2 to max)
  float redStartAngle = thresholdPlusMarginAngle * (3.14159 / 180);
  float redEndAngle = endAngle * (3.14159 / 180);
  String redArc = drawArcSegment(125, 125, 105, redStartAngle, redEndAngle, "#F44336", 0.2);
  html += redArc.substring(0, 14) + "id='red-arc' " + redArc.substring(14);

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
  
  html += "        <line id='gauge-needle' x1='125' y1='125' x2='" + String(pointerX) + "' y2='" + String(pointerY) + "' class='gauge-pointer' />\n";
  html += "        <circle cx='125' cy='125' r='10' fill='#333' />\n"; // Pointer pivot
  // Add hidden element to store max pressure value for JavaScript
  html += "        <text id='max-pressure-value' style='display:none;'>" + String(PRESSURE_MAX) + "</text>\n";
  html += "      </svg>\n";
  
  // SVG closing tag is now moved above
  html += "    </div>\n";

  server.sendContent(html);

  // Add backflush status and manual trigger button
  html = "    <div class='status'>";
  
  // Backflush active section - initially visible or hidden based on current state
  html += "    <div id='backflush-active-section' style='" + String(backflushActive ? "display:block;" : "display:none;") + "'>";
  unsigned long elapsedTime = backflushActive ? (millis() - backflushStartTime) / 1000 : 0;
  html += "      <p class='active'>BACKFLUSH ACTIVE: <span id='backflush-status'>" + String(elapsedTime) + "/" + String(backflushDuration) + " seconds</span></p>";
  html += "      <form method='POST' action='/stopbackflush' onsubmit='return confirm(\"Stop backflush now?\");'>";
  html += "        <button type='submit' class='button' style='background-color: #f44336; margin-top: 10px;'>Stop Backflush</button>";
  html += "      </form>";
  html += "    </div>";
  
  // Backflush inactive section - initially visible or hidden based on current state
  html += "<div id='backflush-inactive-section' style='" + String(backflushActive ? "display:none;" : "display:block;") + "'>";
  html += "<p>Backflush threshold: <span id='backflush-threshold'>" + String(backflushThreshold, 1) + "</span> bar</p>";
  html += "<form method='POST' action='/manualbackflush' onsubmit='return confirm(\"Start backflush now?\");'>";
  html += "<button type='submit' class='button' style='background-color: #4CAF50; margin-top: 10px;'>Backflush Now</button></form></div>";
  server.sendContent(html);

  // Add next scheduled backflush info if available
  time_t nextScheduleTime;
  unsigned int nextScheduleDuration;
  if (scheduler.getNextScheduledTime(nextScheduleTime, nextScheduleDuration)) {
    struct tm* timeinfo = localtime(&nextScheduleTime);
    char timeStr[30];
    strftime(timeStr, sizeof(timeStr), "%A, %B %d at %H:%M", timeinfo);
    
    html = F(R"HTML(
    <div class='next-schedule' style='margin: 20px auto; max-width: 600px; padding: 10px; background-color: #e8f5e9; border-radius: 8px;'>
      <h3 style='margin-top: 0;'>Next Scheduled Backflush</h3>
      <p><strong>)HTML");
    html += String(timeStr);
    html += F(R"HTML(</strong> for )HTML");
    html += String(nextScheduleDuration);
    html += F(R"HTML( seconds</p>
    </div>
    )HTML");
    server.sendContent(html);
  }

  // Add navigation links
  html = F(R"HTML(
    <div class='navigation'>
      <p>
        <a href='/log' style='margin-right: 15px;'>Backflush Log</a>
        <a href='/pressure' style='margin-right: 15px;'>Pressure History</a>
        <a href='/schedule' style='margin-right: 15px;'>Schedule</a>
        <a href='/settings' style='margin-right: 15px;'>Settings</a>
        <a href='/wifi'>WiFi Settings</a>
      </p>
    </div>
  )HTML");
  server.sendContent(html);
  
  // Add backflush configuration form
  html = F(R"HTML(
    <div class='backflush-config'>
      <h2>Backflush Configuration</h2>
      <form id='backflushForm'>
        <div class='form-group'>
          <label for='threshold'>Threshold (bar):</label>
          <input type='number' id='threshold' name='threshold' min='0.2' max=')HTML"); 
  server.sendContent(html);
  server.sendContent(String(PRESSURE_MAX) + "' step='0.1' value='" + String(backflushThreshold, 1));
  html = F(R"HTML('>
        </div>
        <div class='form-group'>
          <label for='duration'>Duration (sec):</label>
          <input type='number' id='duration' name='duration' min='5' max='300' step='1' value=')HTML");
  server.sendContent(html);
  server.sendContent(String(backflushDuration));
  html = F(R"HTML('>
        </div>
        <button type='button' onclick='saveConfig()'>Save Configuration</button>
        <p id='configStatus'></p>
      </form>
    </div>
  )HTML");
  server.sendContent(html);

  html = "    <p>API: <a href='/api'>/api</a> (JSON format)</p>\n";
  
  // Add uptime and current time at the bottom
  html += "    <div class='info'>\n";
  html += "      <p>Uptime: <span id='uptime'>Loading...</span></p>\n";
  if (timeManager.isTimeInitialized()) {
    int offsetHours = timeManager.getTimezoneOffset() / 3600;
    html += "      <p>Current time: <span id='current-time'>" + timeManager.getFormattedDateTime() + "</span> (GMT" + (offsetHours >= 0 ? "+" : "") + String(offsetHours) + ")</p>\n";
  } else {
    html += "      <p>Current time: <span id='current-time'>Loading...</span> (GMT+0)</p>\n";
  }
  html += "    <p>" + String(ESP.getFreeHeap()) + " bytes free</div>\n";
  html += "  </div>\n";
  server.sendContent(html);
  
  


  server.sendContent("</body></html>");
  server.sendContent("");
}

void WebServer::handleAPI() {
  String action = server.arg("action");
  
  if (action == "getschedules") {
    // Return all schedules as JSON
    String json = scheduler.getSchedulesAsJson();
    server.send(200, "application/json", json);
  }
  else {
    // Default API response with system status
    String json = "{";
    json += "\"pressure\":" + String(currentPressure, 2) + ",";
    
    // Use NTP time if available, otherwise use uptime
    if (timeManager.isTimeInitialized()) {
      json += "\"uptime\":" + String(millis() / 1000) + ",";
      json += "\"datetime\":\"" + timeManager.getFormattedDateTime() + "\",";
    } else {
      json += "\"timestamp\":" + String(millis() / 1000) + ",";
    }
    json += "\"backflush_threshold\":" + String(backflushThreshold, 2) + ",";
    json += "\"backflush_duration\":" + String(backflushDuration) + ",";
    json += "\"backflush_active\":" + String(backflushActive ? "true" : "false");
    
    if (backflushActive) {
      unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
      json += ",\"backflush_elapsed\":" + String(elapsedTime);
    }
    
    // Add next scheduled backflush if available
    time_t nextScheduleTime;
    unsigned int nextScheduleDuration;
    if (scheduler.getNextScheduledTime(nextScheduleTime, nextScheduleDuration)) {
      json += ",\"next_scheduled_backflush\":" + String(nextScheduleTime);
      json += ",\"next_scheduled_duration\":" + String(nextScheduleDuration);
    }
    
    json += "}";
    server.send(200, "application/json", json);
  }
}

void WebServer::handleBackflushConfig() {
  if (server.hasArg("threshold") && server.hasArg("duration")) {
    float newThreshold = server.arg("threshold").toFloat();
    unsigned int newDuration = server.arg("duration").toInt();
    
    // Validate values
    if (newThreshold >= 0.2 && newThreshold <= PRESSURE_MAX && 
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
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  String html = F(R"HTML(<!DOCTYPE html>
  <html>
  <head>
    <title>Backflush Event Log</title>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
      .container { max-width: 800px; margin: 0 auto; }
      h1 { color: #2c3e50; }
      .events-table { width: 100%; border-collapse: collapse; margin: 20px 0; }
      .events-table th, .events-table td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
      .events-table th { background-color: #f5f5f5; }
      .events-table tr:hover { background-color: #f9f9f9; }
      .button { display: inline-block; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; }
      .button.danger { background-color: #e74c3c; }
      .button:hover { opacity: 0.9; }
      .info { font-size: 14px; color: #666; margin-top: 40px; }
    </style>
  </head>
  <body>
    <div class='container'>
      <h1>Backflush Event Log</h1>)HTML");
  
  // Add event count
  html += "    <p>Total events: " + String(backflushLogger.getEventCount()) + "</p>\n";
  server.send(200, "text/html", html);
  
  // Add events table
  server.sendContent(backflushLogger.getEventsAsHtml());
  
  // Add navigation and action buttons
  html = "    <p>\n";
  html += "      <a href='/' class='button'>Back to Dashboard</a>\n";
  html += "      <a href='/clearlog' class='button danger' onclick='return confirm(\"Are you sure you want to clear all log entries?\")'>Clear Log</a>\n";
  html += "    </p>\n";
  
  html += "  </div>\n";

  // Add current time if available
  if (timeManager.isTimeInitialized()) {
    int offsetHours = timeManager.getTimezoneOffset() / 3600;
    html += "    <p class='info'>Current time: " + timeManager.getFormattedDateTime() + " (GMT" + (offsetHours >= 0 ? "+" : "") + String(offsetHours) + ")</p>\n";
  }
  html += "</body>\n";
  html += "</html>";
  
  server.sendContent(html);
  server.sendContent("");
}

void WebServer::handleClearLog() {
    backflushLogger.clearEvents();
    server.sendHeader("Location", "/log");
    server.send(303); // Redirect back to log page
}

void WebServer::handlePressureHistory() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    String html = F(R"HTML(<!DOCTYPE html>
    <html>
    <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <!--meta http-equiv="refresh" content="10">-->
    <title>Pool Pressure History</title>
    <link rel='stylesheet' href='/style.css'>
    <style>
    #chart-container { width: 100%; height: 300px; margin: 20px 0; }
        .info { font-size: 14px; color: #666; margin-top: 40px; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@3.9.1/dist/chart.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@2.0.0/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-zoom@1.2.1/dist/chartjs-plugin-zoom.min.js"></script>
    </head>
    <body>
    <h1>Pool Pressure History</h1>)HTML");
    server.send(200, "text/html", html);
    
    // Add navigation links
    html = "<p><a href=\"/\">Back to Dashboard</a> | ";
    html += "<a href=\"/log\">View Backflush Log</a> | ";
    html += "<a href=\"/pressure.csv\" style=\"background-color: #4CAF50; color: white; padding: 6px 12px; border-radius: 4px; text-decoration: none; margin-right: 10px;\">Export CSV</a> | ";
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
    server.sendContent(html);
    
    // Add JavaScript for chart with streaming data
    server.sendContent("<script>");
    server.sendContent("var pressureData = [];\n");
    
    // Add current pressure and time
    unsigned long utcTime = timeManager.getCurrentGMTTime();
    server.sendContent("var currentPressure = " + String(currentPressure, 2) + ";\n");
    server.sendContent("var currentTime = " + String(utcTime) + ";\n");
    
    // Add loading indicator and chart update function
    server.sendContent(F(R"HTML(
      var loading = true;
      var pressureChart = null;
      var chunkSize = 50;
      
      // Function to update the chart with current data
      function updateChart() {
        // Only update if we have data and not still loading
        if (loading) return;
        
        // Clear the container
        document.getElementById('chart-container').innerHTML = '<canvas id="pressure-chart"></canvas>';
        
        if (pressureData.length === 0) {
          document.getElementById('chart-container').innerHTML = '<p>No pressure readings recorded yet.</p>';
          return;
        }
        
        // Prepare data for chart
        var chartData = {
          datasets: [{
            label: 'Pressure (bar)',
            data: pressureData.map(reading => ({
              x: reading.time * 1000, // Convert to milliseconds
              y: parseFloat(reading.pressure.toFixed(2))
            })),
            borderColor: 'rgb(75, 192, 192)',
            borderWidth: 2,
            tension: 0.3,
            pointRadius: 3,
            pointHoverRadius: 5,
            fill: false,
            cubicInterpolationMode: 'monotone'
          }]
        };
        
        console.log('Chart data prepared:', JSON.stringify(chartData, null, 2));
        
        // Calculate Y-axis range
        pressures = chartData.datasets[0].data.map(p => p.y);
        minPressure = Math.min(...pressures) - 0.1;
        maxPressure = Math.max(...pressures) + 0.1;
        
        // Add current pressure as a separate point if we have a valid reading
        if (currentPressure > 0 && currentTime > 0) {
          // Use current time in milliseconds
          var nowTimestamp = currentTime * 1000;
          
          chartData.datasets[0].data.push({
            x: nowTimestamp,
            y: currentPressure
          });
          
          // Update min/max pressure to include current reading
          minPressure = Math.min(minPressure, currentPressure);
          maxPressure = Math.max(maxPressure, currentPressure);
        }
        
        var padding = Math.max(0.1, (maxPressure - minPressure) * 0.1); // 10% padding
        
        // Create chart
        const ctx = document.getElementById('pressure-chart').getContext('2d');
        if (window.pressureChart) {
          window.pressureChart.destroy();
        }

        window.pressureChart = new Chart(ctx, {
          type: 'line',
          data: chartData,
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              x: {
                type: 'time',
                time: {
                  unit: 'hour',
                  tooltipFormat: 'MMM d, yyyy HH:mm',
                  displayFormats: {
                    minute: 'HH:mm',
                    hour: 'MMM d HH:mm',
                    day: 'MMM d',
                    week: 'MMM d',
                    month: 'MMM yyyy'
                  },
                  minUnit: 'minute'
                },
                title: { display: true, text: 'Time' }
              },
              y: {
                title: { display: true, text: 'Pressure (bar)' },
                min: minPressure, max: maxPressure
              }
            },
            plugins: {
              tooltip: {
                callbacks: {
                  label: function(context) {
                    return `Pressure: ${context.parsed.y.toFixed(2)} bar`;
                  }
                }
              },
              zoom: {
                zoom: {
                  wheel: { enabled: true, speed: 0.1 },
                  drag: {
                    enabled: true,
                    backgroundColor: 'rgba(75, 192, 192, 0.2)',
                    borderColor: 'rgb(75, 192, 192)'
                  },
                  pinch: { enabled: true },
                  mode: 'xy',
                  onZoomComplete: ({ chart }) => {
                    chart.update('none');
                  }
                },
                pan: { 
                  enabled: false,
                  mode: 'xy',
                  threshold: 10
                },
                limits: { y: { min: 0, max: maxPressure * 1.5 } }
              }
            },
            interaction: {
              intersect: false,
              mode: 'nearest',
              axis: 'xy'
            },
            animation: { duration: 0 },
            elements: { line: { tension: 0.3 } }
          }
        });
        
        // Add reset zoom button functionality
        document.getElementById('reset-zoom').addEventListener('click', function() {
          if (pressureChart) {
            pressureChart.resetZoom();
          }
        });
        
        // Function to check for new readings
        function checkForNewReadings() {
          if (!pressureChart || !pressureData.length) return;
          
          // Get the timestamp of the most recent reading we have
          const lastTimestamp = Math.max(...pressureData.map(r => r.time));
          
          fetch(`/api/pressure/readings?since=${lastTimestamp + 1}&limit=${chunkSize}`)
            .then(response => response.json())
            .then(data => {
              if (data.readings && data.readings.length > 0) {
                console.log(`Found ${data.readings.length} new readings`);
                
                pressureData = pressureData.concat(data.readings);
                
                // Update the chart
                pressureChart.data.datasets[0].data = pressureData.map(r => ({
                  x: r.time * 1000,
                  y: r.pressure
                }));
                
                // Update the y-axis range if needed
                const pressures = pressureData.map(r => r.pressure);
                const minPressure = Math.min(...pressures) - 0.1;
                const maxPressure = Math.max(...pressures) + 0.1;
                
                pressureChart.options.scales.y.min = minPressure;
                pressureChart.options.scales.y.max = maxPressure;
                
                // Update the chart without animation
                pressureChart.update('none');
                document.dispatchEvent(new Event('dataLoaded'));
              }
            })
            .catch(error => console.error('Error fetching new readings:', error));
        }
        
        // Check for new readings every 10 seconds
        setInterval(checkForNewReadings, 10000);
        
        // Dispatch event that data is loaded
        document.dispatchEvent(new Event('dataLoaded'));
      }
      
      // Function to load all data in chunks
      function loadAllData() {
        var offset = 0;
        var totalReadings = 0;
        
        // Show loading indicator
        document.getElementById('chart-container').innerHTML = '<p>Loading pressure data...</p>';
        
        // Function to fetch and process a chunk of data
        function fetchChunk() {
          fetch('/api/pressure/readings?offset=' + offset + '&limit=' + chunkSize)
            .then(response => response.json())
            .then(data => {
              if (data.readings && data.readings.length > 0) {
                // Append new readings
                pressureData = pressureData.concat(data.readings);
                totalReadings = data.totalReadings || 0;
                offset += data.readings.length;
                 
                // If we have more data, fetch next chunk
                if (offset < totalReadings) {
                  setTimeout(fetchChunk, 0); // Small delay to allow UI to update
                } else {
                  // All data loaded, update chart
                  loading = false;
                  updateChart();
                }
              } else {
                // No more data
                loading = false;
                updateChart();
              }
            })
            .catch(error => {
              console.error('Error loading data:', error);
              document.getElementById('chart-container').innerHTML = '<p>Error loading data. Please refresh the page to try again.</p>';
            });
        }
        
        // Start loading data
        fetchChunk();
      }
      
      // Start loading data when page loads
      loadAllData();
    
    )HTML"));
    // The chart data is loaded asynchronously via loadAllData()
    html = F(R"HTML(
      if (!pressureData || pressureData.length === 0) {
        document.getElementById('chart-container').innerHTML = '<p>Loading pressure data...</p>';
      }
        </script>
        )HTML");
    server.sendContent(html);

    // Chart will be created by the updateChart() function after data is loaded
    
    // Add summary information section
    html = F(R"HTML(
    <div style="margin-top: 30px;">
      <div id="summary-info" style="padding: 15px; background-color: #f8f9fa; border-radius: 5px; margin-bottom: 20px;">
        <h3>Pressure Data Summary</h3>
        <p>Loading data...</p>
      </div>
      
      <div style="padding: 15px; background-color: #f5f5f5; border-radius: 5px;">
        <h3>Settings</h3>
        <form id="retentionForm">
          <label for="retentionDays" style="width: 220px;">Keep pressure data for: </label>
          <input type="number" id="retentionDays" name="retentionDays" min="1" max="90" value=")HTML");
    server.sendContent(html);    
    server.sendContent(String(settings.getDataRetentionDays()));
    server.sendContent(F(R"HTML(" style="width: 60px; padding: 3px;"> days
          <button type="button" onclick="saveRetentionSettings()" class="btn" style="margin-left: 10px;">Save</button>
          <p><small>Data older than this will be automatically pruned. Valid range: 1-90 days.</small></p>
          <p id="retentionStatus" style="font-weight: bold; margin-top: 10px;"></p>
          </form>
          <div class='settings-form'> <form> <div class='form-group'>
                <label for='threshold' style="width: 220px;">Pressure Change Threshold (bar):</label>
                <input type='number' id='threshold' name='threshold' min='0.01' max='1.0' step='0.01' value=')HTML")); 
      server.sendContent(String(settings.getPressureChangeThreshold(), 2));
      server.sendContent(F(R"HTML('>
                <button type="button" onclick="savePressureThreshold()" class='btn'>Save</button>
                <p><small>Pressure must change by this amount to be logged (default: 0.17 bar)</small></p>
                <p id="thresholdStatus" style="font-weight: bold; margin-top: 10px;"></p>
              </div> </form> </div>
          <div class='settings-form'> <form> <div class='form-group'>
                <label for='pressureMaxInterval' style="width: 220px;">Pressure Max Interval (minutes):</label>
                <input type='number' id='pressureMaxInterval' name='pressureMaxInterval' min='1' max='1440' step='1' value=')HTML"));
      server.sendContent(String(settings.getPressureChangeMaxInterval()));
      server.sendContent(F(R"HTML('>
              <button type="button" onclick="savePressureMaxInterval()" class='btn'>Save</button>
              <p id="pressureMaxIntervalStatus" style="font-weight: bold; margin-top: 10px;"></p>
            </div></form> </div>
      </div>
    </div>
    
    <script>
      // Function to update summary information
      function updateSummaryInfo() {
        if (pressureData.length === 0) {
          document.getElementById('summary-info').innerHTML = '<h3>Pressure Data Summary</h3><p>No pressure data available.</p>';
          return;
        }
        
        // Calculate statistics
        const firstReading = pressureData[0];
        const lastReading = pressureData[pressureData.length - 1];)HTML"));
        
      server.sendContent(F(R"HTML(
        // Find min/max pressure
        let minPressure = firstReading.pressure;
        let maxPressure = firstReading.pressure;
        let totalPressure = 0;
        
        pressureData.forEach(reading => {
          if (reading.pressure < minPressure) minPressure = reading.pressure;
          if (reading.pressure > maxPressure) maxPressure = reading.pressure;
          totalPressure += reading.pressure;
        });
        
        const avgPressure = totalPressure / pressureData.length;
        
        // Format dates
        const firstDate = new Date(firstReading.time * 1000);
        const lastDate = new Date(lastReading.time * 1000);
        const dateFormatOptions = { 
          year: 'numeric', 
          month: 'short', 
          day: 'numeric',
          hour: '2-digit', 
          minute: '2-digit',
          hour12: true
        };
        
        // Format dates as strings
        const formatDate = (date) => {
          return date.toLocaleString(undefined, dateFormatOptions);
        };
      )HTML"));
      server.sendContent(F(R"HTML(
        // Create the summary HTML
        const summaryHTML = `
          <h3>Pressure Data Summary</h3>
          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; margin-top: 10px;">
            <div style="background: white; padding: 10px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1);">
              <div style="font-size: 0.9em; color: #666; margin-bottom: 5px;">Total Readings</div>
              <div style="font-size: 1.5em; font-weight: bold;">${pressureData.length.toLocaleString()}</div>
            </div>
            <div style="background: white; padding: 10px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1);">
              <div style="font-size: 0.9em; color: #666; margin-bottom: 5px;">Date Range</div>
              <div style="font-size: 1.1em;">
                ${formatDate(firstDate)}<br>to<br>${formatDate(lastDate)}
              </div>
            </div>
            <div style="background: white; padding: 10px; border-radius: 4px; box-shadow: 0 1px 3px rgba(0,0,0,0.1);">
              <div style="font-size: 0.9em; color: #666; margin-bottom: 5px;">Pressure Range</div>
              <div style="font-size: 1.1em;">
                <span style="color: #e74c3c;">${minPressure.toFixed(2)}</span> to 
                <span style="color: #e74c3c;">${maxPressure.toFixed(2)}</span> bar
              </div>
              <div style="margin-top: 5px; font-size: 0.9em;">
                Average: <strong>${avgPressure.toFixed(2)}</strong> bar
              </div>
            </div>
          </div>
        `;
        
        // Update the DOM
        document.getElementById('summary-info').innerHTML = summaryHTML;
      }
      )HTML"));
      server.sendContent(F(R"HTML(
      // Update summary when data is loaded
      document.addEventListener('dataLoaded', updateSummaryInfo);
      
      
      function saveParameter(endpoint, valueElement, statusElement) {
        const value = document.getElementById(valueElement).value;
        const status = document.getElementById(statusElement);
        
        fetch(endpoint, {
          method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded', },
          body: valueElement + '=' + encodeURIComponent(value)
        })
        .then(response => response.json())
        .then(data => {
          status.textContent = data.message;
          status.style.color = data.success ? '#27ae60' : '#e74c3c';
          // Hide message after 5 seconds
          setTimeout(() => { status.textContent = ''; }, 5000);
        })
        .catch(error => {
          status.textContent = 'Error saving ' + valueElement + ': ' + error;
          status.style.color = '#e74c3c';
        });
      }
      function savePressureThreshold() {
        saveParameter('/setpressurethreshold', 'threshold', 'thresholdStatus');
      }
      function saveRetentionSettings() {
        saveParameter('/setretention', 'retentionDays', 'retentionStatus');
      }
      function savePressureMaxInterval() {
        saveParameter('/setpressuremaxinterval', 'pressureMaxInterval', 'pressureMaxIntervalStatus');
      }
    </script>
    )HTML"));
    
    // Add timezone information and close HTML
    String timeInfo = "<p class='info'>Current time: ";
    timeInfo += timeManager.getCurrentTimeStr();
    timeInfo += " (GMT";
    int offsetHours = timeManager.getTimezoneOffset() / 3600;
    if (offsetHours >= 0) timeInfo += "+";
    timeInfo += String(offsetHours);
    timeInfo += ")</p>\n";
    
    timeInfo += "</body>\n";
    timeInfo += "</html>\n";
    
    server.sendContent(timeInfo);
    server.sendContent("");
}

void WebServer::handleClearPressureHistory() {
    pressureLogger.clearReadings();
    server.sendHeader("Location", "/pressure");
    server.send(303); // Redirect back to pressure history page
}

void WebServer::handleWiFiConfigPage() {

    if (server.method() == HTTP_POST) {
        if (server.hasArg("action")) {
            String action = server.arg("action");
            if (action == "reset") {
                server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/'></head><body><h1>Resetting WiFi settings...</h1><p>The device will restart in configuration mode. You will be redirected shortly.</p></body></html>");
                delay(1000);
                WiFi.disconnect(true);
                // Consider saving a flag to EEPROM/NVS to indicate AP mode on next boot
                delay(1000);
                ESP.restart();
            } else if (action == "connect") {
                String selectedSsid = server.arg("ssid");
                String manualSsid = server.arg("manual_ssid");
                String password = server.arg("password");
                String finalSsid = manualSsid; // Prioritize manual SSID

                if (manualSsid.length() == 0 && selectedSsid.length() > 0) { // If manual is empty, use selected
                    finalSsid = selectedSsid;
                }

                if (finalSsid.length() > 0) {
                    String connectingHtml = "<html><head><meta http-equiv='refresh' content='10;url=/wifi'></head><body><h1>Connecting to " + finalSsid + "...</h1><p>Please wait. You will be redirected back to the WiFi page in 10 seconds.</p></body></html>";
                    server.send(200, "text/html", connectingHtml);
                    delay(100); // Allow server to send response
                    
                    Serial.println("Attempting to connect to SSID: " + finalSsid);
                    WiFi.disconnect(true); // Disconnect from any current network or AP mode
                    delay(500);
                    WiFi.mode(WIFI_STA);
                    WiFi.begin(finalSsid.c_str(), password.c_str());
                    
                    // Add logic here to save credentials if needed (e.g., to EEPROM/NVS)
                    // For now, relying on ESP32's default behavior or WiFiManager if used previously.
                    // If using WiFiManager, it usually handles saving credentials automatically.
                    // If not, you'd call something like: preferences.begin("wifi-creds", false); preferences.putString("ssid", finalSsid); preferences.putString("password", password); preferences.end();

                    // The page will auto-refresh. The status will be visible on the main /wifi page then.
                    // A more robust solution would be to wait for connection status here and display a specific success/failure message.
                } else {
                    server.send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5;url=/wifi'></head><body><h1>Connection Error</h1><p>SSID cannot be empty. Please select a network or enter an SSID manually. Redirecting...</p></body></html>");
                }
                return; // Important to return after handling POST to prevent sending the form page again
            }
        }
    }

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    String html = F(R"HTML(
<!DOCTYPE html>
<html>
<head>
  <title>WiFi Settings</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }
    .container { max-width: 600px; margin: 0 auto; }
    h1 { color: #2c3e50; }
    .info { margin: 20px 0; padding: 15px; background-color: #f8f9fa; border-radius: 5px; }
    .button { display: inline-block; padding: 12px 24px; background-color: #e74c3c; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; font-weight: bold; }
    .button:hover { background-color: #c0392b; }
    .back-link { display: block; margin-top: 30px; color: #3498db; }
    .info { font-size: 14px; color: #666; margin-top: 40px; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>WiFi Settings</h1>
    
    <div class='info'>
      <p>Current WiFi Network: <strong>)HTML"); 
    server.send(200, "text/html", html);
    server.sendContent(WiFi.SSID() + "</strong></p><p>IP Address: " + WiFi.localIP().toString());
    server.sendContent("</p><p>Signal Strength: " + String(WiFi.RSSI()) + " dBm</p>");
    server.sendContent(R"HTML(</div>
    <h2>Connect to a New Network</h2>
    <form method='POST' action='/wifi'>
      <label for='ssid'>Select Network:</label><br>
      <select name='ssid' id='ssid' style='padding: 8px; margin-bottom: 10px; width: 100%; max-width: 300px;'>
        )HTML");    

    // For GET requests, or if POST was not handled above, send the main page HTML
    // If it's a GET request, perform the scan and populate the dropdown
    if (server.method() == HTTP_GET) {
        Serial.println("Scanning for WiFi networks...");
        int n = WiFi.scanNetworks(false, true); // (async, show_hidden)
        Serial.print(n); Serial.println(" networks found");
        String options = "";
        if (n == 0) {
            options = "<option value='' disabled>No networks found</option>";
        } else {
            // Create a vector of network info to sort by signal strength
            struct NetworkInfo {
                String ssid;
                int32_t rssi;
            };
            std::vector<NetworkInfo> networks;
            for (int i = 0; i < n; ++i) {
                if (WiFi.SSID(i).length() > 0) {
                    networks.push_back({WiFi.SSID(i), WiFi.RSSI(i)});
                }
            }
            
            // Sort networks by RSSI (strongest first)
            std::sort(networks.begin(), networks.end(), 
                [](const NetworkInfo& a, const NetworkInfo& b) {
                    return a.rssi > b.rssi;
                });
            
            // Remove duplicate SSIDs, keeping only the strongest signal
            std::vector<NetworkInfo> uniqueNetworks;
            std::set<String> seenSSIDs;
            
            for (const auto& network : networks) {
                if (seenSSIDs.find(network.ssid) == seenSSIDs.end()) {
                    // This is the first occurrence of this SSID (and strongest due to sorting)
                    uniqueNetworks.push_back(network);
                    seenSSIDs.insert(network.ssid);
                }
            }
            
            // Replace the original vector with the deduplicated one
            networks = std::move(uniqueNetworks);
            
            options = "<option value=''>-- Select a Network --</option>";
            for (const auto& network : networks) {
                options += "<option value='" + network.ssid + "'>" + network.ssid + 
                          " (" + String(network.rssi) + " dBm)</option>";
            }
        }
        server.sendContent(options);
        // Replace the placeholder/scanning message with actual network options
        WiFi.scanDelete(); // Free memory from scan results
    }
    html = F(R"HTML(
        </select><br><br>
        <label for='manual_ssid'>Or Enter SSID Manually:</label><br>
        <input type='text' id='manual_ssid' name='manual_ssid' style='padding: 8px; margin-bottom: 10px; width: calc(100% - 18px); max-width: 282px;'><br><br>
        <label for='password'>Password:</label><br>
        <input type='password' id='password' name='password' style='padding: 8px; margin-bottom: 20px; width: calc(100% - 18px); max-width: 282px;'><br><br>
        <button type='submit' name='action' value='connect' class='button' style='background-color: #28a745;'>Connect to WiFi</button>
      </form><br>
  
      <h2>Reset Current Settings</h2>
      <div class='info'>
        <p>Alternatively, you can reset all device settings.</p>
        <p>The device will restart in factory new configuration mode, creating a WiFi access point named <strong>PoolFilterAP</strong>.</p>
        <p>Connect to this network and navigate to <strong>192.168.4.1</strong> to configure your new WiFi settings.</p>
      </div>
      <form method='POST' action='/wifi' onsubmit='return confirm("Are you sure you want to reset all settings? The device will restart.");'>
        <button type='submit' name='action' value='reset' class='button'>Factory Reset</button>
      </form>
      
      <a href='/' class='back-link'>Back to Home</a>
    </div>
  </body>
  </html>
  )HTML");
    server.sendContent(html);
    server.sendContent("");
}

extern bool needManualBackflush;
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
    needManualBackflush = true;
    
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
    needManualBackflush = false;
    
    // Turn off relay and LED
    digitalWrite(RELAY_PIN, LOW);  // Deactivate relay
    digitalWrite(LED_PIN, HIGH);   // Turn LED OFF (inverse logic on NodeMCU)
    Serial.println("Manual backflush stopped");
    
    Serial.println("Backflush stopped manually");
    Serial.print("Actual duration: ");
    Serial.print(elapsedTime);
    Serial.println(" seconds");
    
    // Redirect back to main page
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Redirecting to main page");
}

void WebServer::handleSettings() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  String html = F(R"HTML(
<!DOCTYPE html>
<html>
<head>
  <title>Settings</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
    .container { max-width: 1000px; margin: 0 auto; }
    h1 { color: #2c3e50; }
    .settings-form { margin: 30px 0; padding: 20px; background-color: #f5f5f5; border-radius: 10px; }
    .form-group { margin-bottom: 15px; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; }
    input[type=number] { width: 80px; padding: 5px; }
    button { background-color: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; margin: 5px; }
    .button { display: inline-block; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; }
    .button:hover { opacity: 0.9; }
    .status { margin-top: 10px; font-weight: bold; }
    .calibration-table { width: 100%; border-collapse: collapse; margin: 20px 0; }
    .calibration-table th, .calibration-table td { border: 1px solid #ddd; padding: 8px; text-align: left; }
    .calibration-table th { background-color: #f2f2f2; }
    .calibration-table tr:nth-child(even) { background-color: #f9f9f9; }
    .calibration-table input { width: 80px; padding: 5px; }
  </style>
  <script>
    function saveSensorConfig() {
      const form = document.getElementById('sensorForm');
      const formData = new FormData(form);
      
      // Add calibration data
      const rows = document.querySelectorAll('#calibrationTable tbody tr');
      rows.forEach((row, index) => {
        const voltage = row.querySelector('input[type="number"]').value;
        const pressure = row.querySelectorAll('input[type="number"]')[1].value;
        formData.append(`cal_v${index}`, voltage);
        formData.append(`cal_p${index}`, pressure);
      });
      
      fetch('/sensorconfig', {
        method: 'POST',
        body: formData
      })
      .then(response => response.text())
      .then(message => {
        const status = document.getElementById('configStatus');
        status.textContent = message;
        status.style.color = 'green';
      })
      .catch(error => {
        const status = document.getElementById('configStatus');
        status.textContent = 'Error: ' + error;
        status.style.color = 'red';
      });
    }
    
    function resetCalibration() {
      if (confirm('Are you sure you want to reset all calibration points to default values?')) {
        fetch('/resetcalibration', { method: 'POST' })
          .then(response => response.text())
          .then(message => {
            alert(message);
            location.reload();
          })
          .catch(error => {
            alert('Error resetting calibration: ' + error);
          });
      }
    }
  </script>
</head>
<body>
  <div class='container'>
    <h1>Settings</h1>
    
    <div class='settings-form'>
      <h2>Pressure Sensor Configuration</h2>
      <p>Configure your pressure sensor calibration and settings.</p>
      
      <form id='sensorForm'>
        <div class='form-group'>
          <label for='sensormax'>Maximum Pressure (bar):</label>
          <input type='number' id='sensormax' name='sensormax' min='1.0' max='30.0' step='0.5' value=')HTML");
  server.send(200, "text/html", html);
  html = String(PRESSURE_MAX, 1) + R"HTML('>
          <p><small>Common values: 4.0 bar, 6.0 bar, 10.0 bar depending on your sensor type</small></p>
        </div>
        
        <h3>Calibration Table</h3>
        <p>Calibrate your pressure sensor by entering voltage and corresponding pressure values.</p>
        <table class='calibration-table' id='calibrationTable' style='width: auto; border-collapse: collapse; margin: 15px 0;'>
          <thead>
            <tr style='background-color: #f2f2f2;'>
              <th style='padding: 10px; text-align: left; border-bottom: 1px solid #ddd;'>Point</th>
              <th style='padding: 10px; text-align: left; border-bottom: 1px solid #ddd;'>Voltage (V)</th>
              <th style='padding: 10px; text-align: left; border-bottom: 1px solid #ddd;'>Pressure (bar)</th>
            </tr>
          </thead>
          <tbody>)HTML";
          
  // Add calibration table rows
  const CalibrationPoint* calTable = settings.getCalibrationTable();
  for (int i = 0; i < NUM_CALIBRATION_POINTS; i++) {
    String rowBg = (i % 2 == 0) ? "#fff" : "#f9f9f9";
    html += "<tr style='background-color: " + rowBg + ";'>";
    html += "<td style='padding: 10px; border-bottom: 1px solid #ddd;'>" + String(i+1) + "</td>";
    
    String voltageStr = String(calTable[i].voltage, 3);
    String pressureStr = String(calTable[i].pressure, 1);
    
    html += "<td style='padding: 5px 8px; border-bottom: 1px solid #ddd;'><input type='number' min='0' max='5' step='0.001' value='" + voltageStr + "' style='width: 80px; padding: 4px; box-sizing: border-box;'></td>";
    html += "<td style='padding: 5px 8px; border-bottom: 1px solid #ddd;'><input type='number' min='0' max='30' step='0.1' value='" + pressureStr + "' style='width: 70px; padding: 4px; box-sizing: border-box;'></td></tr>";
  }
  
  html += R"HTML(
          </tbody>
        </table>
        
        <div style='margin: 25px 0;'>
          <button type='button' onclick='saveSensorConfig()' style='padding: 10px 20px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px;'>Save Configuration</button>
          <button type='button' onclick='resetCalibration()' style='padding: 10px 20px; background-color: #e74c3c; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-left: 15px;'>Reset to Default</button>
          <span id='configStatus' style='margin-left: 20px; font-weight: bold; color: #2ecc71;'></span>
        </div>

        <div style='margin: 30px 0; padding: 15px; background-color: #f8f9fa; border-radius: 5px; border-left: 4px solid #3498db;'>
          <h3 style='margin-top: 0; color: #2c3e50;'>Calibration Instructions</h3>
          <ol style='margin-bottom: 0;'>
            <li style='margin-bottom: 8px;'>Apply known pressures to the sensor and note the voltage readings.</li>
            <li style='margin-bottom: 8px;'>Enter the voltage and corresponding pressure values in the table above.</li>
            <li style='margin-bottom: 8px;'>Ensure voltage values are in ascending order (from lowest to highest).</li>
            <li>Click 'Save Configuration' to apply the calibration settings.</li>
          </ol>
        </div>
      </form>
    </div>

    <div style='margin: 40px 0; background-color: #f5f5f5; padding: 25px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.05);'>
      <h2 style='margin-top: 0; color: #2c3e50; border-bottom: 1px solid #e0e0e0; padding-bottom: 10px;'>Sensor Debug Information</h2>
      <table style='width: 100%; border-collapse: collapse; margin: 15px 0;'>
        <tr>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0;'><strong>Raw ADC Value:</strong></td>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0; font-family: monospace;'>)HTML";
          
  server.sendContent(html);
  html = String(rawADCValue) + " / 1023";
  
  html += R"HTML(</td>
        </tr>
        <tr>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0;'><strong>Voltage:</strong></td>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0; font-family: monospace;'>)HTML";
  
  html += String(sensorVoltage, 3) + " V";
  
  html += R"HTML(</td>
        </tr>
        <tr>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0;'><strong>Pressure:</strong></td>
          <td style='padding: 10px; border-bottom: 1px solid #e0e0e0; font-family: monospace;'>)HTML";
  
  html += String(currentPressure, 2) + " bar";
  
  html += R"HTML(</td>
        </tr>
      </table>
      <p style='margin: 15px 0 0 0; font-style: italic; color: #666; font-size: 0.9em;'>This information updates when you refresh the page</p>
    </div>
  </div>

    <div class='settings-form'>
      <h2>Software Update</h2>
      <p>Version: <a target="_blank" href='https://github.com/0w1nn3r/pool-filter-pressure-reader/commit/)HTML" + getGitSha() + "'><code>" + getGitSha() + R"HTML(</code></a></p>
      <p>Built: )HTML" + String(BUILD_DATE) + " " + String(BUILD_TIME) + R"HTML(</p>
      <p>You can update the device's software using the Over-The-Air (OTA) update feature.</p>
      <p>Device hostname: )HTML" + String(HOSTNAME) + ".local</p>";
  server.sendContent(html);
  html = F(R"HTML(
      <p>Update options:</p>
      <div style='margin: 20px 0;'>
        <h3>Option 1: IDE Upload</h3>
        <ol>
          <li>Click 'Enable OTA Updates'</li>
          <li>Use PlatformIO or Arduino IDE to upload new firmware within 5 minutes</li>

        </ol>
        <button type='button' onclick='enableOTA()' class='button'>Enable OTA Updates</button>
        <p id='otaStatus'></p>
      </div>
      <div style='margin: 20px 0;'>
        <h3>Option 2: Web Upload</h3>
        <ol>
          <li>Click the button below to go to the web uploader</li>
          <li>Select a firmware .bin file and upload it directly</li>
        </ol>
        <a href='/otaupload' class='button' style='background-color: #e67e22;'>Web Firmware Uploader</a>
      </div>
    </div>
<p><a href='/' class='button'>Back to Home</a></p>

      </div>
      
      <script>

      function enableOTA() {
        const status = document.getElementById('otaStatus');
        status.textContent = 'Enabling OTA updates...';
        status.style.color = 'blue';
        
        fetch('/ota', {
          method: 'POST',
        })
        .then(response => response.text())
        .then(data => {
          status.textContent = data;
          status.style.color = 'green';
        })
        .catch(error => {
          status.textContent = 'Error: ' + error;
          status.style.color = 'red';
        });
      }
    </script>
  </div>
</body>
</html>
)HTML");
  server.sendContent(html);
  server.sendContent("");
}

void WebServer::handleSensorConfig() {
    String message = "Failed to update sensor settings";
    
    // Process sensor max pressure
    if (server.hasArg("sensormax")) {
        String sensorMaxStr = server.arg("sensormax");
        float sensorMax = sensorMaxStr.toFloat();
        
        // Validate input
        if (sensorMax >= 1.0 && sensorMax <= 30.0) {
            settings.setSensorMaxPressure(sensorMax);
            PRESSURE_MAX = sensorMax;  // Update global variable
            message = "Sensor settings updated successfully";
            
            Serial.println("Max pressure updated to: " + String(sensorMax, 1) + " bar");
        } else {
            message = "Error: Invalid pressure range (1.0-30.0 bar)";
            server.send(400, "text/plain", message);
            return;
        }
    }
    
    // Process calibration points
    bool calUpdated = false;
    for (int i = 0; i < NUM_CALIBRATION_POINTS; i++) {
        String vKey = "cal_v" + String(i);
        String pKey = "cal_p" + String(i);
        
        if (server.hasArg(vKey) && server.hasArg(pKey)) {
            float voltage = server.arg(vKey).toFloat();
            float pressure = server.arg(pKey).toFloat();
            
            // Validate the calibration point
            if (voltage < 0 || voltage > 5.0 || pressure < 0 || pressure > 30.0) {
                message = "Error: Invalid calibration values. Voltage: 0-5V, Pressure: 0-30 bar";
                server.send(400, "text/plain", message);
                return;
            }
            
            // Update the calibration point
            if (!settings.setCalibrationPoint(i, voltage, pressure)) {
                message = "Error: Calibration points must be in ascending voltage order";
                server.send(400, "text/plain", message);
                return;
            }
            
            calUpdated = true;
        }
    }
    
    // Save calibration if any points were updated
    if (calUpdated) {
        if (settings.saveCalibration()) {
            message = "Calibration updated successfully";
            
            // Log the new calibration table
            Serial.println("Calibration table updated:");
            const CalibrationPoint* calTable = settings.getCalibrationTable();
            for (int i = 0; i < NUM_CALIBRATION_POINTS; i++) {
                Serial.printf("  Point %d: %.3fV -> %.1f bar\n", 
                             i, calTable[i].voltage, calTable[i].pressure);
            }
        } else {
            message = "Error: Failed to save calibration";
            server.send(500, "text/plain", message);
            return;
        }
    }
    
    server.send(200, "text/plain", message);
}

// Handle calibration reset
void WebServer::handleResetCalibration() {
    // Reset to default calibration by calling reset() which will reload defaults
    settings.reset();
    // Reload the calibration to ensure it's in memory
    settings.loadCalibration();
    server.send(200, "text/plain", "Calibration reset to default values");
}

void WebServer::handlePressureReadingsApi() {
    String json;
    // Check for 'since' parameter first
    if (server.hasArg("since")) {
        time_t since = server.arg("since").toInt();
        int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 100;
        
        // Get readings since the specified timestamp (most recent first)
        std::vector<PressureReading> newReadings = pressureLogger.getReadingsSince(since, limit);
        
        // Create JSON response
        DynamicJsonDocument doc(4096);
        JsonArray readingsArray = doc.createNestedArray("readings");
        
        // Convert readings to JSON (already in reverse chronological order)
        for (const auto& reading : newReadings) {
            JsonObject readingObj = readingsArray.add<JsonObject>();
            readingObj["time"] = reading.timestamp;
            readingObj["pressure"] = reading.pressure;
            
            // Add formatted time string
            char timeStr[20];
            struct tm* timeinfo = localtime(&reading.timestamp);
            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
            readingObj["timeStr"] = String(timeStr);
        }
        
        // Add metadata
        doc["count"] = newReadings.size();
        doc["success"] = true;
        
        serializeJson(doc, json);
    } else {
        // Original pagination logic
        int offset = server.arg("offset").toInt();
        int limit = server.arg("limit").toInt();
        
        // Set default values if not provided
        offset = max(0, offset);
        limit = (limit < 1 || limit > 100) ? 50 : limit; // Max 100 readings per request
        
        // Get paginated readings - we'll use the existing function but with offset/limit
        int totalPages = 0;
        int page = (offset / limit) + 1; // Convert offset to page number
        json = pressureLogger.getPaginatedReadingsAsJson(page, limit, totalPages);
    }
    // Send response with no-cache headers
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send(200, "application/json", json);
}

void WebServer::handlePressureCsv() {
    Serial.printf("[Memory] handlePressureCsv start: %d bytes free\n", ESP.getFreeHeap());
    // FIXME chunk this into 2k chunks
    String csv = pressureLogger.getReadingsAsCsv();
    
    // Generate filename with current date
    time_t now = timeManager.getCurrentTime();
    struct tm* timeinfo = localtime(&now);
    char filename[32];
    strftime(filename, sizeof(filename), "pressure_%Y%m%d.csv", timeinfo);
    
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Disposition", "attachment; filename=" + String(filename));
    server.send(200, "text/csv", csv);
    Serial.printf("[Memory] handlePressureCsv end: %d bytes free\n", ESP.getFreeHeap());
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
    
    String jsonResponse = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    server.send(200, "application/json", jsonResponse);
}

void WebServer::handleSetPressureThreshold() {
    bool success = false;
    String message = "Failed to update pressure threshold";
    if (server.hasArg("threshold")) {
        float newThreshold = server.arg("threshold").toFloat();
        if (newThreshold > 0 && newThreshold <= 1.0) {
            settings.setPressureChangeThreshold(newThreshold);
            success = true;
            message = "Pressure change threshold updated to " + String(newThreshold, 2) + " bar";
        }
        else {
            message = "Invalid pressure threshold. Must be between 0 and 1 bar.";
        }
    }
    String jsonResponse = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    server.send(200, "application/json", jsonResponse);
}

void WebServer::handleSetPressureMaxInterval() {
    bool success = false;
    String message = "Failed to update pressure max interval";
    if (server.hasArg("pressureMaxInterval")) {
        unsigned int newInterval = server.arg("pressureMaxInterval").toInt();
        if (newInterval >= 1 && newInterval <= 1440) {
            settings.setPressureChangeMaxInterval(newInterval);
            success = true;
            message = "Pressure change max interval updated to " + String(newInterval) + " minutes";
        }
        else {
            message = "Invalid pressure max interval. Must be between 1 and 1440 minutes.";
        }
    }
    String jsonResponse = "{\"success\":" + String(success ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    server.send(200, "application/json", jsonResponse);
}

void WebServer::handleOTAUploadPage() {
    String html = F(R"HTML(
<!DOCTYPE html>
<html>
<head>
  <title>OTA Firmware Update</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; text-align: center; }
    .container { max-width: 600px; margin: 0 auto; }
    .upload-form { margin: 20px 0; padding: 20px; border: 1px solid #ddd; border-radius: 5px; }
    .btn { background-color: #4CAF50; border: none; color: white; padding: 10px 20px; text-align: center; text-decoration: none; display: inline-block; font-size: 16px; margin-top: 20px; cursor: pointer; border-radius: 5px; }
    .warning { color: #f44336; }
    .progress { width: 100%; background-color: #f1f1f1; border-radius: 5px; margin: 10px 0; display: none; }
    .progress-bar { width: 0%; height: 30px; background-color: #4CAF50; border-radius: 5px; text-align: center; line-height: 30px; color: white; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>OTA Firmware Update</h1>
    <p>Upload a new firmware file (.bin) to update the device.</p>
    <div class='upload-form'>
      <form method='POST' action='/otaupload' enctype='multipart/form-data' id='upload_form'>
        <p><input type='file' name='update' accept='.bin'></p>
        <p><button type='submit' class='btn'>Update Firmware</button></p>
      </form>
      <div class='progress' id='progress'>
        <div class='progress-bar' id='progress-bar'>0%</div>
      </div>
      <p id='status'></p>
    </div>
    <p class='warning'>Warning: Do not disconnect or power off the device during update!</p>
    <p><a href='/settings'>Back to Settings</a></p>
  </div>

  <script>
    document.getElementById('upload_form').addEventListener('submit', function(e) {
      e.preventDefault();
      var form = document.getElementById('upload_form');
      var formData = new FormData(form);
      var xhr = new XMLHttpRequest();
      var progressBar = document.getElementById('progress-bar');
      var progressDiv = document.getElementById('progress');
      var statusDiv = document.getElementById('status');
      progressDiv.style.display = 'block';
      xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
          var percent = Math.round((e.loaded / e.total) * 100);
          progressBar.style.width = percent + '%';
          progressBar.innerHTML = percent + '%';
          statusDiv.innerHTML = 'Uploading firmware: ' + percent + '%';
        }
      });
      xhr.addEventListener('load', function(e) {
        if (xhr.status === 200) {
          statusDiv.innerHTML = 'Upload complete. Device is restarting...';
          // Redirect to home page after 5 seconds
          setTimeout(function() {
            window.location.href = '/';
          }, 5000);
        } else {
          statusDiv.innerHTML = 'Error: ' + xhr.responseText;
        }
      });
      xhr.addEventListener('error', function(e) {
        statusDiv.innerHTML = 'Upload failed';
      });
      xhr.open('POST', '/otaupload', true);
      xhr.send(formData);
    });
  </script>
</body>
</html>
)HTML");
    
    server.send(200, "text/html", html);
}



void WebServer::handleOTAUpload() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        
        // Start with max available size
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) {
            Update.printError(Serial);
        }
        
        // Show initial update screen on OLED
        if (display) {
            display->showFirmwareUpdateProgress(0);
        }
    } 
    else if (upload.status == UPLOAD_FILE_WRITE) {
        // Calculate progress percentage based on Update library
        int progress = (Update.progress() * 100) / Update.size();
        
        Serial.printf("Upload progress: %d%%\n", progress);
        
        // Update OLED display with progress
        if (display) {
            display->showFirmwareUpdateProgress(progress);
        }
        
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } 
    else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
          Serial.printf("Update Success: %u bytes\n", upload.totalSize);
          
          // Show 100% on OLED
          if (display) {
              display->showFirmwareUpdateProgress(100);
          }
          
          // Send response with redirect after 5 seconds
          server.sendHeader("Connection", "close");
          server.sendHeader("Access-Control-Allow-Origin", "*");
          server.send(200, "text/html", 
              "<!DOCTYPE html><html><head><title>Update Success</title>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>body{font-family:Arial,sans-serif;margin:20px;text-align:center;}"
              "h1{color:#4CAF50;}</style>"
              "<meta http-equiv='refresh' content='5;url=/'>"
              "</head><body>"
              "<h1>Update Successful!</h1>"
              "<p>Device will restart now.</p>"
              "<p>You will be redirected to the home page in 5 seconds...</p>"
              "</body></html>");
          
          // Restart ESP after a short delay
          delay(1000);
          ESP.restart();
      } else {
          Update.printError(Serial);
          server.send(500, "text/plain", "UPDATE FAILED");
      }
  } 
  else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      Serial.println("Update aborted");
      server.send(400, "text/plain", "Update aborted");
  }
    // Avoid timeout issues during upload
    yield();
}

void WebServer::handleSchedulePage() {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    String html = F(R"HTML(<!DOCTYPE html>
    <html>
    <head>
        <title>Backflush Schedule</title>
        <meta name='viewport' content='width=device-width, initial-scale=1'>
        <link rel='stylesheet' href='/style.css'>
        <style>
            .schedule-form { background-color: #f5f5f5; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
            .schedule-list { margin-top: 30px; }
            .schedule-item { background-color: #f9f9f9; padding: 15px; border-radius: 8px; margin-bottom: 10px; }
            .schedule-item.disabled { opacity: 0.6; }
            .form-row { margin-bottom: 10px; display: flex; align-items: center; flex-wrap: wrap; }
            .form-row label { min-width: 120px; margin-right: 10px; }
            .form-row .days-select { display: flex; flex-wrap: wrap; gap: 5px; margin-top: 5px; }
            .form-row .days-select label { min-width: auto; margin-right: 5px; }
            .form-row .time-input { display: flex; align-items: center; }
            .form-row .time-input input { width: 50px; margin-right: 5px; }
            .button-row { margin-top: 20px; display: flex; gap: 10px; }
            .button-primary { background-color: #4CAF50; }
            .button-danger { background-color: #f44336; }
            .button-secondary { background-color: #2196F3; }
            .hidden { display: none; }
            .next-schedule { margin-top: 20px; padding: 10px; background-color: #e8f5e9; border-radius: 8px; }
        </style>
    </head>
    <body>
        <h1>Backflush Schedule</h1>
        <p><a href="/">Back to Dashboard</a></p>
)HTML");
    server.send(200, "text/html", html);

    // Check if we have a next scheduled backflush
    time_t nextScheduleTime;
    unsigned int nextScheduleDuration;
    if (scheduler.getNextScheduledTime(nextScheduleTime, nextScheduleDuration)) {
        struct tm* timeinfo = localtime(&nextScheduleTime);
        char timeStr[30];
        strftime(timeStr, sizeof(timeStr), "%A, %B %d at %H:%M", timeinfo);
        
        html = "<div class='next-schedule'>";
        html += "<h3>Next Scheduled Backflush</h3>";
        html += "<p><strong>" + String(timeStr) + "</strong> for " + String(nextScheduleDuration) + " seconds</p>";
        html += "</div>";
        server.sendContent(html);
    }

    // Add form for creating new schedule
    html = F(R"HTML(
        <h2>Add New Schedule</h2>
        <div class="schedule-form">
            <form id="scheduleForm" action="/scheduleupdate" method="POST">
                <input type="hidden" name="id" id="scheduleId" value="-1">
                
                <div class="form-row">
                    <label for="enabled">Enabled:</label>
                    <input type="checkbox" id="enabled" name="enabled" checked>
                </div>
                
                <div class="form-row">
                    <label for="scheduleType">Schedule Type:</label>
                    <select id="scheduleType" name="type" onchange="updateFormFields()">
                        <option value="daily">Daily</option>
                        <option value="weekly">Weekly</option>
                        <option value="monthly">Monthly</option>
                    </select>
                </div>
                
                <div class="form-row">
                    <label for="time">Time:</label>
                    <div class="time-input">
                        <input type="number" id="hour" name="hour" min="0" max="23" value="12" required> : 
                        <input type="number" id="minute" name="minute" min="0" max="59" value="0" required>
                    </div>
                </div>
                
                <div class="form-row" id="weekdaysRow">
                    <label>Days of Week:</label>
                    <div class="days-select">
                        <label><input type="checkbox" name="weekday" value="0"> Sunday</label>
                        <label><input type="checkbox" name="weekday" value="1"> Monday</label>
                        <label><input type="checkbox" name="weekday" value="2"> Tuesday</label>
                        <label><input type="checkbox" name="weekday" value="3"> Wednesday</label>
                        <label><input type="checkbox" name="weekday" value="4"> Thursday</label>
                        <label><input type="checkbox" name="weekday" value="5"> Friday</label>
                        <label><input type="checkbox" name="weekday" value="6"> Saturday</label>
                    </div>
                </div>
                
                <div class="form-row hidden" id="monthdaysRow">
                    <label>Days of Month:</label>
                    <div class="days-select" id="monthdaysSelect">
                        <!-- Will be populated by JavaScript -->
                    </div>
                </div>
                
                <div class="form-row">
                    <label for="duration">Duration (sec):</label>
                    <input type="number" id="duration" name="duration" min="5" max="300" value="30" required>
                </div>
                
                <div class="button-row">
                    <button type="submit" class="button button-primary">Save Schedule</button>
                    <button type="button" id="cancelButton" class="button button-secondary hidden">Cancel</button>
                </div>
            </form>
        </div>
        
        <h2>Current Schedules</h2>
        <div id="scheduleList" class="schedule-list">
)HTML");
    server.sendContent(html);

    // Get all schedules and display them
    String scheduleList = "";
    size_t count = scheduler.getScheduleCount();
    if (count == 0) {
        scheduleList = "<p>No schedules defined.</p>";
    } else {
        for (size_t i = 0; i < count; i++) {
            BackflushSchedule schedule = scheduler.getSchedule(i);
            scheduleList += "<div class='schedule-item" + String(schedule.enabled ? "" : " disabled") + "'>";
            scheduleList += "<h3>Schedule " + String(i + 1) + "</h3>";
            
            // Schedule type
            scheduleList += "<p><strong>Type:</strong> ";
            switch (schedule.type) {
                case ScheduleType::DAILY:
                    scheduleList += "Daily";
                    break;
                case ScheduleType::WEEKLY:
                    scheduleList += "Weekly";
                    break;
                case ScheduleType::MONTHLY:
                    scheduleList += "Monthly";
                    break;
            }
            scheduleList += "</p>";
            
            // Time
            scheduleList += "<p><strong>Time:</strong> " + 
                            String(schedule.hour) + ":" + 
                            (schedule.minute < 10 ? "0" : "") + String(schedule.minute) + 
                            "</p>";
            
            // Days
            if (schedule.type == ScheduleType::WEEKLY) {
                scheduleList += "<p><strong>Days:</strong> ";
                const char* weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
                bool first = true;
                for (int day = 0; day < 7; day++) {
                    if (schedule.daysActive & (1 << day)) {
                        if (!first) scheduleList += ", ";
                        scheduleList += weekdays[day];
                        first = false;
                    }
                }
                scheduleList += "</p>";
            } else if (schedule.type == ScheduleType::MONTHLY) {
                scheduleList += "<p><strong>Days:</strong> ";
                bool first = true;
                for (int day = 0; day < 31; day++) {
                    if (schedule.daysActive & (1 << day)) {
                        if (!first) scheduleList += ", ";
                        scheduleList += String(day + 1);
                        first = false;
                    }
                }
                scheduleList += "</p>";
            }
            
            // Duration
            scheduleList += "<p><strong>Duration:</strong> " + String(schedule.duration) + " seconds</p>";
            
            // Status
            scheduleList += "<p><strong>Status:</strong> " + String(schedule.enabled ? "Enabled" : "Disabled") + "</p>";
            
            // Edit/Delete buttons
            scheduleList += "<div class='button-row'>";
            scheduleList += "<button class='button button-secondary' onclick='editSchedule(" + String(i) + ")'>Edit</button>";
            scheduleList += "<form method='POST' action='/scheduledelete' style='display:inline;'>";
            scheduleList += "<input type='hidden' name='id' value='" + String(i) + "'>";
            scheduleList += "<button type='submit' class='button button-danger' onclick='return confirm(\"Are you sure you want to delete this schedule?\")'>Delete</button>";
            scheduleList += "</form>";
            scheduleList += "</div>";
            
            scheduleList += "</div>";
        }
    }
    server.sendContent(scheduleList);

    // Add JavaScript for form handling
    html = F(R"HTML(
        </div>
        
        <script>
            // Populate month days
            const monthdaysSelect = document.getElementById('monthdaysSelect');
            for (let i = 1; i <= 31; i++) {
                const label = document.createElement('label');
                const checkbox = document.createElement('input');
                checkbox.type = 'checkbox';
                checkbox.name = 'monthday';
                checkbox.value = i - 1; // 0-based index
                label.appendChild(checkbox);
                label.appendChild(document.createTextNode(' ' + i));
                monthdaysSelect.appendChild(label);
            }
            
            // Function to update form fields based on schedule type
            function updateFormFields() {
                const scheduleType = document.getElementById('scheduleType').value;
                const weekdaysRow = document.getElementById('weekdaysRow');
                const monthdaysRow = document.getElementById('monthdaysRow');
                
                weekdaysRow.classList.add('hidden');
                monthdaysRow.classList.add('hidden');
                
                if (scheduleType === 'weekly') {
                    weekdaysRow.classList.remove('hidden');
                } else if (scheduleType === 'monthly') {
                    monthdaysRow.classList.remove('hidden');
                }
            }
            
            // Initialize form fields
            updateFormFields();
            
            // Function to edit a schedule
            function editSchedule(id) {
                // Get schedule data from JSON
                fetch('/api?action=getschedules')
                    .then(response => response.json())
                    .then(data => {
                        const schedule = data.schedules.find(s => s.id === id);
                        if (!schedule) return;
                        
                        // Update form fields
                        document.getElementById('scheduleId').value = id;
                        document.getElementById('enabled').checked = schedule.enabled;
                        document.getElementById('scheduleType').value = schedule.type;
                        document.getElementById('hour').value = schedule.hour;
                        document.getElementById('minute').value = schedule.minute;
                        document.getElementById('duration').value = schedule.duration;
                        
                        // Update days checkboxes
                        if (schedule.type === 'weekly') {
                            const weekdayCheckboxes = document.getElementsByName('weekday');
                            for (let i = 0; i < weekdayCheckboxes.length; i++) {
                                const day = parseInt(weekdayCheckboxes[i].value);
                                weekdayCheckboxes[i].checked = (schedule.daysActive & (1 << day)) !== 0;
                            }
                        } else if (schedule.type === 'monthly') {
                            const monthdayCheckboxes = document.getElementsByName('monthday');
                            for (let i = 0; i < monthdayCheckboxes.length; i++) {
                                const day = parseInt(monthdayCheckboxes[i].value);
                                monthdayCheckboxes[i].checked = (schedule.daysActive & (1 << day)) !== 0;
                            }
                        }
                        
                        // Update form visibility
                        updateFormFields();
                        
                        // Show cancel button
                        document.getElementById('cancelButton').classList.remove('hidden');
                        
                        // Scroll to form
                        document.querySelector('.schedule-form').scrollIntoView({ behavior: 'smooth' });
                    });
            }
            
            // Cancel button handler
            document.getElementById('cancelButton').addEventListener('click', function() {
                document.getElementById('scheduleForm').reset();
                document.getElementById('scheduleId').value = -1;
                document.getElementById('cancelButton').classList.add('hidden');
                updateFormFields();
            });
        </script>
    </body>
    </html>
)HTML");
    server.sendContent(html);
    server.sendContent("");
}

void WebServer::handleScheduleUpdate() {
    int id = server.arg("id").toInt();
    bool isNew = (id == -1);
    
    // Create schedule object from form data
    BackflushSchedule schedule;
    schedule.enabled = server.hasArg("enabled");
    
    // Parse schedule type
    String typeStr = server.arg("type");
    if (typeStr == "daily") {
        schedule.type = ScheduleType::DAILY;
    } else if (typeStr == "weekly") {
        schedule.type = ScheduleType::WEEKLY;
    } else if (typeStr == "monthly") {
        schedule.type = ScheduleType::MONTHLY;
    }
    
    // Parse time
    schedule.hour = constrain(server.arg("hour").toInt(), 0, 23);
    schedule.minute = constrain(server.arg("minute").toInt(), 0, 59);
    
    // Parse days active
    schedule.daysActive = 0;
    if (schedule.type == ScheduleType::WEEKLY) {
        // Process weekday checkboxes
        for (int i = 0; i < server.args(); i++) {
            if (server.argName(i) == "weekday") {
                int day = server.arg(i).toInt();
                if (day >= 0 && day < 7) {
                    schedule.daysActive |= (1 << day);
                }
            }
        }
    } else if (schedule.type == ScheduleType::MONTHLY) {
        // Process monthday checkboxes
        for (int i = 0; i < server.args(); i++) {
            if (server.argName(i) == "monthday") {
                int day = server.arg(i).toInt();
                if (day >= 0 && day < 31) {
                    schedule.daysActive |= (1 << day);
                }
            }
        }
    }
    
    // Parse duration
    schedule.duration = constrain(server.arg("duration").toInt(), 5, 300);
    
    if (isNew) {
        scheduler.addSchedule(schedule);
    } else {
        scheduler.updateSchedule(id, schedule);
    }
    
    // Redirect back to schedule page
    server.sendHeader("Location", "/schedule");
    server.send(303);
}

void WebServer::handleScheduleDelete() {
    int id = server.arg("id").toInt();
    scheduler.deleteSchedule(id);
    
    // Redirect back to schedule page
    server.sendHeader("Location", "/schedule");
    server.send(303);
}
