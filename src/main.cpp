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
#include "BackflushScheduler.h"

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin not used on most modules
#define SCREEN_ADDRESS 0x3C  // I2C address for SSD1306 (0x3C for 128x64)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pressure Sensor Configuration
#define PRESSURE_PIN A0  // Analog pin for pressure sensor
const float PRESSURE_MIN = 0.0;   // Minimum pressure in bar
float PRESSURE_MAX = 4.0;  // Maximum pressure in bar (will be updated from settings)
const float VOLTAGE_MIN = 0.5;    // Minimum voltage output (V)
const float VOLTAGE_MAX = 3.3;    // Maximum voltage output (V)
const float ADC_RESOLUTION = 1024.0;  // 10-bit ADC resolution

// WiFi Configuration
#define WIFI_AP_NAME "PoolPressure-Setup"

// Reset Button Configuration
#define RESET_BUTTON_PIN D7  // GPIO13 (D7) for reset button - changed from D3 to avoid boot mode issues

// Backflush Relay Configuration
const int RELAY_PIN = D5;          // GPIO14 (D5) for backflush relay

// Onboard LED Configuration
const int LED_PIN = D4;   // Onboard LED (usually GPIO2/D4 on NodeMCU)

// System components

WebServer* webServer;
Display* displayManager;
TimeManager* timeManager;
Settings* settings;
BackflushLogger* backflushLogger;
PressureLogger* pressureLogger;
BackflushScheduler* scheduler;

// Variables
float currentPressure = 0.0;
int rawADCValue = 0;      // Store the raw ADC reading
float sensorVoltage = 0.0; // Store the voltage reading
unsigned long lastReadTime = 0;
const unsigned long readInterval = 1000;  // Read pressure every 1 second

// Backflush configuration
float backflushThreshold = 2.0;  // Default threshold in bar
unsigned int backflushDuration = 30;  // Default duration in seconds
bool backflushActive = false;
unsigned long backflushStartTime = 0;
float backflushTriggerPressure = 0.0;  // Store the pressure that triggered the backflush
bool backflushConfigChanged = false;
String currentBackflushType = "Auto";  // Track whether the current backflush is Auto or Manual
bool needManualBackflush = false;

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
  digitalWrite(RELAY_PIN, HIGH);  // Ensure relay is off at startup
  
  // Initialize onboard LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED is off when HIGH on NodeMCU (inverse logic)
  Serial.println("LED initialized");
  
  // Initialize settings
  settings = new Settings();
  settings->begin();
  
  // Load settings
  backflushThreshold = settings->getBackflushThreshold();
  backflushDuration = settings->getBackflushDuration();
  PRESSURE_MAX = settings->getSensorMaxPressure();
  
  Serial.print("Loaded backflush threshold: ");
  Serial.print(backflushThreshold);
  Serial.print(" bar, duration: ");
  Serial.print(backflushDuration);
  Serial.print(" seconds, sensor max pressure: ");
  Serial.print(PRESSURE_MAX);
  Serial.println(" bar");
  
  // Initialize I2C and display
  Wire.begin();
  displayManager = new Display(display, currentPressure, backflushThreshold, 
                             backflushDuration, backflushActive, backflushStartTime, nullptr);
  bool displayInitialized = displayManager->init();
  if (displayInitialized) {
    Serial.println("OLED display initialized successfully");
  } else {
    Serial.println("Running without OLED display");
  }
  
  // Check if reset button is pressed during startup
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    // Wait for 3 seconds while button is held
    unsigned int countdownSeconds = 3;
    unsigned long startTime = millis();
    bool buttonReleased = false;
    
    while (millis() - startTime < countdownSeconds * 1000) {
      if (digitalRead(RESET_BUTTON_PIN) == HIGH) {
        buttonReleased = true;
        break;
      }
      
      // Update countdown display
      int remainingSeconds = countdownSeconds - ((millis() - startTime) / 1000);
      displayManager->showResetCountdown("Hold for factory reset", remainingSeconds);
      delay(100);  // Small delay to prevent display flicker
    }
    
    // If button was held for full duration, reset settings
    if (!buttonReleased) {
      resetSettings();
    }
    
    // If button was released early, clear display and continue normal boot
    display.clearDisplay();
    display.display();
  }
  
  // Show startup screen
  displayManager->showStartupScreen();
  
  // Setup WiFi
  setupWiFi();
  
  // Initialize time manager after WiFi is connected
  timeManager = new TimeManager();
  timeManager->begin();
  
  displayManager->setTimeManager(timeManager);
  displayManager->showTimezone();

  // Initialize backflush logger
  backflushLogger = new BackflushLogger(*timeManager);
  backflushLogger->begin();
  
  // Initialize pressure logger
  pressureLogger = new PressureLogger(*timeManager, *settings);
  pressureLogger->begin();
  
  // Initialize backflush scheduler
  scheduler = new BackflushScheduler(*timeManager);
  scheduler->begin();
  
  // Initialize web server
  webServer = new WebServer(currentPressure, rawADCValue, sensorVoltage, backflushThreshold, backflushDuration, 
                            backflushActive, backflushStartTime, backflushConfigChanged,
                            currentBackflushType, *timeManager, *backflushLogger, *settings, *pressureLogger, *scheduler);
  webServer->begin();
  
  // Connect components for bidirectional communication
  displayManager->setWebServer(webServer);
  displayManager->setScheduler(scheduler);
  webServer->setDisplay(displayManager);
  
  delay(2000);  // Display startup message for 2 seconds
}

void loop() {
  // Update time from NTP server
  timeManager->update();
  
  // Handle WiFi and server
  webServer->handleClient();
  
  // Check for scheduled backflush if time is initialized
  static unsigned long lastScheduleCheck = 0;
  static unsigned long lastDisplayUpdate = 0;
  
  if (timeManager->isTimeInitialized()) {
    unsigned long currentMillis = millis();
    
    // Check for scheduled backflush every 30 seconds
    if (currentMillis - lastScheduleCheck >= 30000) {
      lastScheduleCheck = currentMillis;
      
      // Check if a scheduled backflush should be triggered
      unsigned int scheduledDuration = 0;
      if (!backflushActive && scheduler->checkSchedules(timeManager->getCurrentTime(), scheduledDuration)) {
        // Set the backflush duration to the scheduled duration
        backflushDuration = scheduledDuration;
        
        // Set flag for scheduled backflush
        currentBackflushType = "Scheduled";
        needManualBackflush = true; // Use the manual backflush flag to trigger it
      }
    }
    
    // Update the next scheduled backflush display every minute
    if (displayManager && displayManager->isDisplayAvailable() && 
        (currentMillis - lastDisplayUpdate >= 60000 || lastDisplayUpdate == 0)) {
      lastDisplayUpdate = currentMillis;
      displayManager->updateDisplay();
    }
  }
  
  // Read pressure at regular intervals
  unsigned long currentTime = millis();
  if (currentTime - lastReadTime >= readInterval) {
    bool resetButtonPressed = digitalRead(RESET_BUTTON_PIN) == LOW;
    currentPressure = readPressure();
    if (!resetButtonPressed) displayManager->updateDisplay();
    lastReadTime = currentTime;
    
    // Record pressure reading if time is initialized
    if (timeManager->isTimeInitialized()) {
      pressureLogger->addReading(currentPressure);
      pressureLogger->update(); // Check if we need to save readings
    }

    // Handle reset button - power cycle if held for 3 seconds
    static unsigned long reset_button_pressed_time = 0;
    if (resetButtonPressed) {
      if (reset_button_pressed_time == 0) {
        reset_button_pressed_time = millis();
      }
      int remainingSeconds = 3 - ((millis() - reset_button_pressed_time) / 1000);
      displayManager->showResetCountdown("Hold to restart", remainingSeconds);
      
      if (millis() - reset_button_pressed_time >= 3000) {
        ESP.restart();
      }
    } else {
      reset_button_pressed_time = 0;
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
  
  delay(50);
}

float readPressure() {
  // Read analog value from pressure sensor
  rawADCValue = analogRead(PRESSURE_PIN);
  
  // Convert analog reading to voltage
  sensorVoltage = (rawADCValue / ADC_RESOLUTION) * 3.3;  // ESP8266 ADC is 3.3V reference
  
  // Convert voltage to pressure (bar)
  // Using linear mapping: pressure = (voltage - VOLTAGE_MIN) * (PRESSURE_MAX - PRESSURE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) + PRESSURE_MIN
  float pressure = (sensorVoltage - VOLTAGE_MIN) * (PRESSURE_MAX - PRESSURE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) + PRESSURE_MIN;
  
  // Constrain pressure to valid range
  pressure = constrain(pressure, PRESSURE_MIN, PRESSURE_MAX);
  
  Serial.print("Raw ADC: ");
  Serial.print(rawADCValue);
  Serial.print(", Voltage: ");
  Serial.print(sensorVoltage);
  Serial.print("V, Pressure: ");
  Serial.print(pressure);
  Serial.println(" bar");
  
  return pressure;
}



void setupWiFi() {
  displayManager->showWiFiConnecting();
  
  // Initialize WiFiManager
  Serial.println("Creating a wifimanager");
  WiFiManager wifiManager;
  
  // Set callback for when entering configuration mode
  wifiManager.setAPCallback([](WiFiManager* mgr) {
    displayManager->showWiFiSetupMode(WIFI_AP_NAME);
  });
  
  // Try to connect using saved credentials
  // If connection fails, it will start an access point with the specified name
  // and go into a blocking loop awaiting configuration
  Serial.println("auto connecting....");
  if (!wifiManager.autoConnect(WIFI_AP_NAME)) {
    Serial.println("Failed to connect and hit timeout");
    
    if (displayManager->isDisplayAvailable()) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println(F("WiFi setup failed"));
      display.println(F("Restarting..."));
      display.display();
    }
    
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
}

void handleBackflush() {
  // Check if backflush should be activated
  if (!backflushActive && (currentPressure >= backflushThreshold || needManualBackflush)) {
    // Start backflush
    backflushActive = true;
    backflushStartTime = millis();
    backflushTriggerPressure = currentPressure; // Store the pressure that triggered the backflush
    
    // If needManualBackflush is true but currentBackflushType is not set, default to "Manual"
    if (needManualBackflush && currentBackflushType != "Scheduled") {
      currentBackflushType = "Manual";
    }
    
    digitalWrite(RELAY_PIN, LOW);  // Activate relay
    digitalWrite(LED_PIN, LOW);    // Turn LED ON (inverse logic on NodeMCU)
    
    // Log the backflush event
    backflushLogger->logEvent(backflushTriggerPressure, backflushDuration, currentBackflushType);
    
    // Log to serial
    Serial.println("\n=== BACKFLUSH STARTED ===");
    Serial.print("Type: ");
    Serial.println(currentBackflushType);
    Serial.print("Trigger Pressure: ");
    Serial.print(backflushTriggerPressure, 1);
    Serial.println(" bar");
    Serial.print("Duration: ");
    Serial.print(backflushDuration);
    Serial.println(" seconds");
    
    // Display message on OLED if available
    if (displayManager && displayManager->isDisplayAvailable()) {
      String message = "Type: " + String(currentBackflushType) + 
                     "\nDuration: " + String(backflushDuration) + "s";
      displayManager->showMessage("Backflush Started", message);
    }
    
    // Reset the flags
    needManualBackflush = false;
    if (currentBackflushType == "Scheduled") {
      // Reset to default for next time
      currentBackflushType = "Auto";
    }
  }
  
  // Check if backflush should be stopped
  if (backflushActive) {
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;  // Convert to seconds
    if (elapsedTime >= backflushDuration) {
      // Stop backflush
      backflushActive = false;
      digitalWrite(RELAY_PIN, HIGH);  // Deactivate relay
      digitalWrite(LED_PIN, HIGH);   // Turn LED OFF (inverse logic on NodeMCU)
      Serial.println("Backflush completed");
      
      // No longer logging backflush end events as per user request
      Serial.print("Backflush completed with trigger pressure: ");
      Serial.print(backflushTriggerPressure, 1);
      Serial.println(" bar");
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
  
  // Clear backflush schedules
  if (scheduler) {
    scheduler->clearSchedules();
  }
  
  Serial.println("RESET BUTTON PRESSED - Clearing all settings and schedules");
  
  // Visual feedback if display is available
  if (displayManager->isDisplayAvailable()) {
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
  } else {
    // Just delay without display
    delay(2500);
  }
  
  Serial.println("All settings cleared. Restarting...");
  delay(2000);
  
  // Restart the ESP
  ESP.restart();
}
