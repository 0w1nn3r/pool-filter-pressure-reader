#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin not used on most modules
#define SCREEN_ADDRESS 0x3C  // I2C address for SSD1306 (0x3C for 128x64)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pressure Sensor Configuration
#define PRESSURE_PIN A0  // Analog pin for pressure sensor
#define PRESSURE_MIN 0.0   // Minimum pressure in bar
#define PRESSURE_MAX 4.0   // Maximum pressure in bar (adjust based on your sensor)
#define VOLTAGE_MIN 0.5    // Minimum voltage output (V)
#define VOLTAGE_MAX 4.5    // Maximum voltage output (V)
#define ADC_RESOLUTION 1024.0  // 10-bit ADC resolution

// WiFi Configuration
#define WIFI_AP_NAME "PoolPressure-Setup"

// Reset Button Configuration
#define RESET_BUTTON_PIN D3  // GPIO0 (D3) for reset button

// Backflush Relay Configuration
#define RELAY_PIN D5          // GPIO14 (D5) for backflush relay

// Web Server
ESP8266WebServer server(80);

// Variables
float currentPressure = 0.0;
unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000;  // Read pressure every 1 second

// Backflush configuration
float backflushThreshold = 2.0;  // Default threshold in bar
unsigned int backflushDuration = 30;  // Default duration in seconds
bool backflushActive = false;
unsigned long backflushStartTime = 0;
bool backflushConfigChanged = false;

// Function prototypes
float readPressure();
void updateDisplay();
void handleRoot();
void handleAPI();
void setupWiFi();
void resetSettings();
void handleBackflush();
void saveBackflushConfig();
void handleBackflushConfig();

void setup() {
  Serial.begin(115200);
  Serial.println("\nPool Filter Pressure Reader Starting...");
  
  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Initialize reset button pin
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Ensure relay is off at startup
  
  // Check if reset button is pressed during startup
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    resetSettings();
  }
  
  // Load backflush configuration from EEPROM
  int address = 100;
  float savedThreshold;
  unsigned int savedDuration;
  
  EEPROM.get(address, savedThreshold);
  address += sizeof(float);
  EEPROM.get(address, savedDuration);
  
  // Validate loaded values
  if (!isnan(savedThreshold) && savedThreshold >= 0.5 && savedThreshold <= PRESSURE_MAX) {
    backflushThreshold = savedThreshold;
  }
  
  if (savedDuration >= 5 && savedDuration <= 300) {
    backflushDuration = savedDuration;
  }
  
  Serial.print("Loaded backflush threshold: ");
  Serial.print(backflushThreshold);
  Serial.print(" bar, duration: ");
  Serial.print(backflushDuration);
  Serial.println(" seconds");
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Pool Filter"));
  display.println(F("Pressure Reader"));
  display.println(F("Initializing..."));
  display.display();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/api", handleAPI);
  server.on("/backflush", HTTP_POST, handleBackflushConfig);
  server.begin();
  Serial.println("HTTP server started");
  
  delay(2000);  // Display startup message for 2 seconds
}

void loop() {
  // Handle WiFi and server
  server.handleClient();
  
  // Read pressure at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= readInterval) {
    currentPressure = readPressure();
    updateDisplay();
    lastReadTime = currentTime;
  }
  
  // Handle backflush control
  handleBackflush();
}

float readPressure() {
  // Read analog value from pressure sensor
  int rawValue = analogRead(PRESSURE_PIN);
  
  // Convert analog reading to voltage
  float voltage = (rawValue / ADC_RESOLUTION) * 3.3;  // ESP8266 ADC is 3.3V reference
  
  // Convert voltage to pressure (bar)
  // Using linear mapping: pressure = (voltage - VOLTAGE_MIN) * (PRESSURE_MAX - PRESSURE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) + PRESSURE_MIN
  float pressure = (voltage - VOLTAGE_MIN) * (PRESSURE_MAX - PRESSURE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) + PRESSURE_MIN;
  
  // Constrain pressure to valid range
  pressure = constrain(pressure, PRESSURE_MIN, PRESSURE_MAX);
  
  Serial.print("Raw ADC: ");
  Serial.print(rawValue);
  Serial.print(", Voltage: ");
  Serial.print(voltage);
  Serial.print("V, Pressure: ");
  Serial.print(pressure);
  Serial.println(" bar");
  
  return pressure;
}

void updateDisplay() {
  display.clearDisplay();
  
  // Display WiFi status in top row with signal bars
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // WiFi status indicator
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    // Draw WiFi signal bars
    if (rssi > -50) {
      display.print(F("WiFi:[||||]"));
    } else if (rssi > -65) {
      display.print(F("WiFi:[||| ]"));
    } else if (rssi > -75) {
      display.print(F("WiFi:[||  ]"));
    } else if (rssi > -85) {
      display.print(F("WiFi:[|   ]"));
    } else {
      display.print(F("WiFi:[    ]"));
    }
    
    // Show IP address
    display.setCursor(70, 0);
    display.print(WiFi.localIP()[3]); // Just show last octet to save space
  } else {
    display.print(F("WiFi:[X]"));
  }
  
  // Display pressure in large font in center
  display.setTextSize(3);
  display.setCursor(10, 20);
  display.print(currentPressure, 1);
  
  // Display bar unit
  display.setTextSize(2);
  display.setCursor(90, 30);
  display.print(F("bar"));
  
  // Display backflush status at bottom
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    display.print(F("BACKFLUSH: "));
    display.print(elapsedTime);
    display.print(F("/"));
    display.print(backflushDuration);
    display.print(F("s"));
  } else {
    display.print(F("Threshold: "));
    display.print(backflushThreshold, 1);
    display.print(F(" bar"));
  }
  
  display.display();
}

void setupWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Connecting to WiFi..."));
  display.display();
  
  // Initialize WiFiManager
  WiFiManager wifiManager;
  
  // Set callback for when entering configuration mode
  wifiManager.setAPCallback([](WiFiManager* mgr) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("WiFi Setup Mode"));
    display.println(F("Connect to:"));
    display.println(WIFI_AP_NAME);
    display.println(F("Then go to:"));
    display.println(F("192.168.4.1"));
    display.display();
  });
  
  // Try to connect using saved credentials
  // If connection fails, it will start an access point with the specified name
  // and go into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(WIFI_AP_NAME)) {
    Serial.println("Failed to connect and hit timeout");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("WiFi setup failed"));
    display.println(F("Restarting..."));
    display.display();
    delay(3000);
    
    // Reset and try again
    ESP.restart();
  }
  
  // If we get here, we're connected to WiFi
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("WiFi Connected!"));
  display.print(F("SSID: "));
  display.println(WiFi.SSID());
  display.print(F("IP: "));
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

void handleRoot() {
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

void handleAPI() {
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

void handleBackflush() {
  // Check if backflush should be activated
  if (!backflushActive && currentPressure >= backflushThreshold) {
    // Start backflush
    backflushActive = true;
    backflushStartTime = millis();
    digitalWrite(RELAY_PIN, HIGH);  // Activate relay
    Serial.println("Backflush started");
  }
  
  // Check if backflush should be stopped
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;  // Convert to seconds
    if (elapsedTime >= backflushDuration) {
      // Stop backflush
      backflushActive = false;
      digitalWrite(RELAY_PIN, LOW);  // Deactivate relay
      Serial.println("Backflush completed");
    }
  }
}

void saveBackflushConfig() {
  // Save backflush configuration to EEPROM
  int address = 100;  // Start at address 100 to avoid conflict with WiFi settings
  
  EEPROM.put(address, backflushThreshold);
  address += sizeof(float);
  
  EEPROM.put(address, backflushDuration);
  
  EEPROM.commit();
  Serial.println("Backflush configuration saved");
}

void handleBackflushConfig() {
  if (server.hasArg("threshold") && server.hasArg("duration")) {
    float newThreshold = server.arg("threshold").toFloat();
    unsigned int newDuration = server.arg("duration").toInt();
    
    // Validate values
    if (newThreshold >= 0.5 && newThreshold <= PRESSURE_MAX && 
        newDuration >= 5 && newDuration <= 300) {
      backflushThreshold = newThreshold;
      backflushDuration = newDuration;
      backflushConfigChanged = true;
      
      // Save to EEPROM
      saveBackflushConfig();
      
      server.send(200, "text/plain", "Configuration updated");
    } else {
      server.send(400, "text/plain", "Invalid values");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void resetSettings() {
  // Display reset message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("RESET BUTTON PRESSED"));
  display.println(F("Clearing WiFi settings"));
  display.display();
  
  // Clear WiFiManager settings
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  
  // Visual feedback
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("RESET BUTTON PRESSED"));
    display.println(F("Clearing WiFi settings"));
    if (i % 2 == 0) {
      display.println(F("*****************"));
    }
    display.display();
    delay(500);
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("WiFi settings cleared"));
  display.println(F("Restarting..."));
  display.display();
  delay(2000);
  
  // Restart the ESP
  ESP.restart();
}
