#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include "WebServer.h"

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin not used on most modules
#define SCREEN_ADDRESS 0x3C  // I2C address for SSD1306 (0x3C for 128x64)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pressure Sensor Configuration
#define PRESSURE_PIN A0  // Analog pin for pressure sensor
#define PRESSURE_MIN 0.0   // Minimum pressure in bar
float PRESSURE_MAX = 4.0;  // Maximum pressure in bar (adjust based on your sensor)
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
WebServer* webServer;

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
void setupWiFi();
void resetSettings();
void handleBackflush();
void saveBackflushConfig();

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
  
  // Initialize web server
  webServer = new WebServer(currentPressure, backflushThreshold, backflushDuration, 
                          backflushActive, backflushStartTime, backflushConfigChanged);
  webServer->begin();
  
  delay(2000);  // Display startup message for 2 seconds
}

void loop() {
  // Handle WiFi and server
  webServer->handleClient();
  
  // Read pressure at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= readInterval) {
    currentPressure = readPressure();
    updateDisplay();
    lastReadTime = currentTime;
  }
  
  // Handle backflush control
  handleBackflush();
  
  // Save backflush config if changed
  if (backflushConfigChanged) {
    saveBackflushConfig();
    backflushConfigChanged = false;
  }
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
