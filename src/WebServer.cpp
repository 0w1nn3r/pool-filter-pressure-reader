#include "WebServer.h"

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
    server.on("/setretention", HTTP_POST, std::bind(&WebServer::handleSetRetention, this));
    server.on("/pressure.csv", [this]() { handlePressureCsv(); });
    
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
    <title>Pool Pressure History</title>
    <link rel='stylesheet' href='/style.css'>
    <style>
    #chart-container { width: 100%; height: 300px; margin: 20px 0; }
        .info { font-size: 14px; color: #666; margin-top: 40px; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/moment@2.29.1/min/moment.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@2.9.4/dist/Chart.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-moment@0.1.2/dist/chartjs-adapter-moment.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/hammerjs@2.0.8/hammer.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-zoom@0.7.7/dist/chartjs-plugin-zoom.min.js"></script>
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
    
    // Add JavaScript for chart
    server.sendContent("<script>var pressureData = ");
    server.sendContent(pressureLogger.getReadingsAsJson());
    server.sendContent(";\n");
    
    html = F(R"HTML(  // Check if we have data
      if (!pressureData || !pressureData.readings || pressureData.readings.length === 0) {
        document.getElementById('chart-container').innerHTML = '<p>No pressure readings recorded yet.</p>';
      } else {
        // Prepare data for Chart.js
        var chartData = [];
        // Sort data by timestamp
        pressureData.readings.sort(function(a, b) { return a.time - b.time; });
        // Process each reading
        for (var i = 0; i < pressureData.readings.length; i++) {
          var reading = pressureData.readings[i];
          // Convert UTC timestamp to local time using detected timezone
          var localTime = new Date(reading.time * 1000);
          chartData.push({
            x: localTime,
            y: reading.pressure
          });})HTML");
    server.sendContent(html);

    html = F(R"HTML(    // Create the chart
        var ctx = document.getElementById('pressure-chart').getContext('2d');
        var chart; // Define chart variable in wider scope for reset button access
        chart = new Chart(ctx, {
          type: 'line',
          data: {
            datasets: [{
              label: 'Pressure (bar)',
              data: chartData,
              backgroundColor: 'rgba(75, 192, 192, 0.2)',
              borderColor: 'rgb(75, 192, 192)',
              tension: 0,
              fill: false
            }]
          },
          options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
              xAxes: [{
                type: 'time',
                time: {
                  displayFormats: {
                    hour: 'MMM D, HH:mm'
                  },
                  timezone: 'Europe/Paris'
                },
                ticks: {
                  maxRotation: 45,
                  minRotation: 45,
                  autoSkip: true,
                  maxTicksLimit: 10
                }
              }],
              yAxes: [{
                ticks: {
                  beginAtZero: false
                }
              }]
            },
            plugins: {
              zoom: {
                pan: {
                  enabled: false
                },
                zoom: {
                  enabled: true,
                  mode: 'xy',
                  drag: true,
                  speed: 0.1,
                  threshold: 2,
                  sensitivity: 3
                }
              }
            }
          }
        });
        // Add reset zoom button functionality
        document.getElementById('reset-zoom').addEventListener('click', function() {
          chart.resetZoom();
        });
        // Also reset zoom on double-click
        document.getElementById('pressure-chart').addEventListener('dblclick', function() {
          chart.resetZoom();
        });
      }
    </script>)HTML");
    server.sendContent(html);
    
    // Add summary information
    html = F(R"HTML(<script>
      // Display summary information if we have data
      if (pressureData && pressureData.readings && pressureData.readings.length > 0) {
        document.write('<p><strong>Total readings:</strong> ' + pressureData.readings.length + '</p>');
        // Convert GMT timestamps to local time
        var firstDate = new Date(pressureData.readings[0].time * 1000);
        var lastDate = new Date(pressureData.readings[pressureData.readings.length-1].time * 1000);
        document.write('<p><strong>Date range:</strong> ' + firstDate.toLocaleString() + ' to ' + lastDate.toLocaleString() + '</p>');
      }
    </script>
    <div style="margin-top: 30px; padding: 15px; background-color: #f5f5f5; border-radius: 5px;">
      <h3>Data Retention Settings</h3>
      <form id="retentionForm">
        <label for="retentionDays">Keep pressure data for: </label>
        <input type="number" id="retentionDays" name="retentionDays" min="1" max="90" value=")HTML");
    server.sendContent(html);    
    server.sendContent(String(settings.getDataRetentionDays()));
    server.sendContent(F(R"HTML(" style="width: 60px;"> days
        <button type="button" onclick="saveRetentionSettings()" style="margin-left: 10px;">Save</button>
        <p><small>Data older than this will be automatically pruned. Valid range: 1-90 days.</small></p>
        <p id="retentionStatus" style="font-weight: bold;"></p>
      </form>
    </div>
    <script>
      function saveRetentionSettings() {
        const retentionDays = document.getElementById('retentionDays').value;
        const status = document.getElementById('retentionStatus');
        
        fetch('/setretention', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'retentionDays=' + retentionDays
        })
        .then(response => response.json())
        .then(data => {
          status.textContent = data.message;
          status.style.color = data.success ? 'green' : 'red';
          setTimeout(() => { status.textContent = ''; }, 3000);
        })
        .catch(error => {
          status.textContent = 'Error: ' + error;
          status.style.color = 'red';
        });
      }
    </script>)HTML"));
 
    int offsetHours = timeManager.getTimezoneOffset() / 3600;
    html = "<p class='info'>Current time: " + timeManager.getCurrentTimeStr() + " (GMT" + (offsetHours >= 0 ? "+" : "") + String(offsetHours) + ")</p>\n";
    
    html += "</body>\n";
    html += "</html>\n";
    
    server.sendContent(html);
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
    .container { max-width: 800px; margin: 0 auto; }
    h1 { color: #2c3e50; }
    .settings-form { margin: 30px 0; padding: 20px; background-color: #f5f5f5; border-radius: 10px; }
    .form-group { margin-bottom: 15px; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; }
    input[type=number] { width: 80px; padding: 5px; }
    button { background-color: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; }
    .button { display: inline-block; padding: 10px 20px; background-color: #3498db; color: white; text-decoration: none; border-radius: 4px; margin-top: 20px; }
    .button:hover { opacity: 0.9; }
    .status { margin-top: 10px; font-weight: bold; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Settings</h1>
    
    <div class='settings-form'>
      <h2>Pressure Sensor Configuration</h2>
      <p>Configure your pressure sensor by setting its maximum pressure range.</p>
      <div style='display: flex; justify-content: space-between;'>
        <div style='flex: 1;'>
          <form id='sensorForm'>
            <div class='form-group'>
              <label for='sensormax'>Maximum Pressure (bar):</label>
              <input type='number' id='sensormax' name='sensormax' min='1.0' max='30.0' step='0.5' value=')HTML");
  server.send(200, "text/html", html);
  html = String(PRESSURE_MAX, 1) + R"HTML('>
              <p><small>Common values: 4.0 bar, 6.0 bar, 10.0 bar depending on your sensor type</small></p>
            </div>
            <button type='button' onclick='saveSensorConfig()'>Save Configuration</button>
            <p id='configStatus'></p>
          </form>
        </div>
        <div style='flex: 1; border-left: 1px solid #ddd; padding-left: 20px; margin-left: 20px;'>
          <h3>Sensor Debug Info</h3>
          <table style='width: 100%; border-collapse: collapse;'>
            <tr><td style='padding: 5px 0;'><strong>Raw ADC Value:</strong></td><td>)HTML";
  server.sendContent(html);
  html = String(rawADCValue) + R"HTML( / 1023</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Voltage:</strong></td><td>)HTML" + String(sensorVoltage, 3) + R"HTML( V</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Pressure:</strong></td><td>)HTML" + String(currentPressure, 2) + R"HTML( bar</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Min Voltage:</strong></td><td>)HTML" + String(VOLTAGE_MIN, 2) + R"HTML( V</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Max Voltage:</strong></td><td>)HTML" + String(VOLTAGE_MAX, 2) + R"HTML( V</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Min Pressure:</strong></td><td>)HTML" + String(PRESSURE_MIN, 1) + R"HTML( bar</td></tr>
            <tr><td style='padding: 5px 0;'><strong>Max Pressure:</strong></td><td>)HTML" + String(PRESSURE_MAX, 1);
  server.sendContent(html);          
  html = R"HTML( bar</td></tr>
          </table>
          <p><small>This information updates when you refresh the page</small></p>
        </div>
      </div>
    </div>

    <div class='settings-form'>
      <h2>Software Update</h2>
      <p>Current Version: )HTML" + String(__DATE__ " " __TIME__) + R"HTML(</p>
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

    <script>
      function saveSensorConfig() {
        const sensormax = document.getElementById('sensormax').value;
        const status = document.getElementById('configStatus');
        
        fetch('/sensorconfig', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: 'sensormax=' + sensormax
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
      }

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
    
    if (server.hasArg("sensormax")) {
        String sensorMaxStr = server.arg("sensormax");
        float sensorMax = sensorMaxStr.toFloat();
        
        // Validate input
        if (sensorMax >= 1.0 && sensorMax <= 30.0) {
            // Update the setting
            settings.setSensorMaxPressure(sensorMax);
            
            // Update the global variable
            PRESSURE_MAX = sensorMax;
            
            message = "Sensor max pressure updated to: " + String(sensorMax, 1) + " bar";
            
            Serial.print("Sensor max pressure updated to: ");
            Serial.println(sensorMax);
        } else {
            message = "Invalid sensor max pressure value. Must be between 1.0 and 30.0 bar.";
        }
    }
    
    // Return JSON response instead of redirecting
    server.send(200, "text/plain", message);
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
    
    // Return JSON response instead of redirecting
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
    
    bool success = false;
    if (isNew) {
        success = scheduler.addSchedule(schedule);
    } else {
        success = scheduler.updateSchedule(id, schedule);
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
