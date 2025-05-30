#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include "WebServer.h"
#include "Display.h"
#include "TimeManager.h"
#include "Settings.h"
#include "BackflushLogger.h"
#include "PressureLogger.h"

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

// System components
WebServer* webServer;
Display* displayManager;
TimeManager* timeManager;
Settings* settings;
BackflushLogger* backflushLogger;
PressureLogger* pressureLogger;

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
void setupWiFi();
void resetSettings();
void handleBackflush();
void saveBackflushConfig();

void setup() {
  Serial.begin(115200);
  Serial.println("\nPool Filter Pressure Reader Starting...");
  
  // Initialize reset button pin
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // Ensure relay is off at startup
  
  // Initialize settings
  settings = new Settings();
  settings->begin();
  
  // Check if reset button is pressed during startup
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    resetSettings();
  }
  
  // Load settings
  backflushThreshold = settings->getBackflushThreshold();
  backflushDuration = settings->getBackflushDuration();
  
  Serial.print("Loaded backflush threshold: ");
  Serial.print(backflushThreshold);
  Serial.print(" bar, duration: ");
  Serial.print(backflushDuration);
  Serial.println(" seconds");
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize Display Manager
  displayManager = new Display(display, currentPressure, backflushThreshold, 
                             backflushDuration, backflushActive, backflushStartTime);
  displayManager->init();
  displayManager->showStartupScreen();
  
  // Setup WiFi
  setupWiFi();
  
  // Initialize time manager
  timeManager = new TimeManager();
  timeManager->begin();
  
  // Initialize backflush logger
  backflushLogger = new BackflushLogger(*timeManager);
  backflushLogger->begin();
  
  // Initialize pressure logger
  pressureLogger = new PressureLogger(*timeManager);
  pressureLogger->begin();
  
  // Initialize web server
  webServer = new WebServer(currentPressure, backflushThreshold, backflushDuration, 
                          backflushActive, backflushStartTime, backflushConfigChanged,
                          *timeManager, *backflushLogger, *settings, *pressureLogger);
  webServer->begin();
  
  delay(2000);  // Display startup message for 2 seconds
}

void loop() {
  // Update time from NTP server
  timeManager->update();
  
  // Handle WiFi and server
  webServer->handleClient();
  
  // Read pressure at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= readInterval) {
    currentPressure = readPressure();
    displayManager->updateDisplay();
    lastReadTime = currentTime;
    
    // Record pressure reading if time is initialized
    if (timeManager->isTimeInitialized()) {
      pressureLogger->addReading(currentPressure);
      pressureLogger->update(); // Check if we need to save readings
    }
  }
  
  // Handle backflush control
  handleBackflush();
  
  // Save backflush config if changed
  if (backflushConfigChanged) {
    settings->setBackflushThreshold(backflushThreshold);
    settings->setBackflushDuration(backflushDuration);
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



void setupWiFi() {
  displayManager->showWiFiConnecting();
  
  // Initialize WiFiManager
  WiFiManager wifiManager;
  
  // Set callback for when entering configuration mode
  wifiManager.setAPCallback([](WiFiManager* mgr) {
    displayManager->showWiFiSetupMode(WIFI_AP_NAME);
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
  
  displayManager->showWiFiConnected(WiFi.SSID(), WiFi.localIP());
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
      
      // Log the backflush event
      if (timeManager->isTimeInitialized()) {
        backflushLogger->logEvent(currentPressure, backflushDuration);
        Serial.println("Backflush event logged");
      } else {
        Serial.println("Backflush event not logged - time not initialized");
      }
    }
  }
}





void resetSettings() {
  // Display reset message
  displayManager->showResetMessage();
  
  // Clear WiFiManager settings
  WiFiManager wifiManager;
  wifiManager.resetSettings();
  
  // Reset settings to defaults
  settings->reset();
  
  // Visual feedback
  for (int i = 0; i < 5; i++) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("RESET BUTTON PRESSED"));
    display.println(F("Clearing all settings"));
    if (i % 2 == 0) {
      display.println(F("*****************"));
    }
    display.display();
    delay(500);
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("All settings cleared"));
  display.println(F("Restarting..."));
  display.display();
  delay(2000);
  
  // Restart the ESP
  ESP.restart();
}
