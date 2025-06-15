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

#ifdef GIT_SHA_STR
  #pragma message("GIT_SHA_STR is defined as: " GIT_SHA_STR)
#else
  #pragma message("GIT_SHA_STR is NOT defined")
#endif

// OLED Display Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset pin not used on most modules
#define SCREEN_ADDRESS 0x3C  // I2C address for SSD1306 (0x3C for 128x64)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pressure Sensor Configuration
#define PRESSURE_PIN A0  // Analog pin for pressure sensor
float PRESSURE_MAX = 2.0;  // Maximum pressure in bar (will be overridden by settings)
const float ADC_REF_VOLTAGE = 1.0f;  // 1.0V for ESP8266 ADC
const float ADC_RESOLUTION = 1024.0f;  // 10-bit ADC resolution

// Number of calibration points (must match Settings.h)
#ifndef NUM_CALIBRATION_POINTS
#define NUM_CALIBRATION_POINTS 10
#endif

// Default calibration table (will be overridden by settings)
const CalibrationPoint DEFAULT_CALIBRATION[NUM_CALIBRATION_POINTS] = {
    {0.112f, 0.0f},    // 0.0 bar at 0.112V
    {0.170f, 0.9f},   // 0.9 bar at 0.170V
    {0.177f, 1.0f},    // 1.0 bar at 0.177V
    {0.190f, 1.1f},    // 1.1 bar at 0.190V
    {0.200f, 1.2f},    // 1.2 bar at 0.200V
    {0.206f, 1.3f},    // 1.3 bar at 0.206V
    {0.210f, 1.4f},   // 1.4 bar at 0.210V
    {0.214f, 1.5f},   // 1.5 bar at 0.214V
    {0.219f, 1.6f},   // 1.6 bar at 0.219V
    {0.240f, 2.0f}     // 2.0 bar at 0.78V
};
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
int rawADCValue = 0;           // Raw ADC value (0-1023)
float sensorVoltage = 0.0;     // Voltage from pressure sensor (V)
float smoothedPressure = 0.0;  // Smoothed pressure value (bar)
unsigned long lastPressureUpdate = 0;  // Last pressure update time (ms)
const unsigned long PRESSURE_UPDATE_INTERVAL = 100; // Update interval (ms)
const float HALF_LIFE = 1.0f;  // Half-life for EMA in seconds
float alpha = 0.0f;           // EMA alpha coefficient (will be calculated)
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
  digitalWrite(RELAY_PIN, LOW);  // Ensure relay is off at startup
  
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
    static bool firstReading = true;
    static unsigned long lastReadTime = 0;
    static float smoothedPressure = 0.0;
    static float alpha = 0.0f;
    
    unsigned long currentTime = millis();
    
    // Only process new readings at the specified interval
    if (currentTime - lastPressureUpdate >= PRESSURE_UPDATE_INTERVAL || firstReading) {
        // Read analog value from pressure sensor
        rawADCValue = analogRead(PRESSURE_PIN);
        
        // Convert analog reading to voltage (0-1.0V for ESP8266 ADC)
        sensorVoltage = (rawADCValue / ADC_RESOLUTION) * ADC_REF_VOLTAGE;
        
        // Get the calibration table from settings
        const CalibrationPoint* calTable = settings->getCalibrationTable();
        float currentPressure = 0.0;
        
        // Find the two closest calibration points
        bool pointFound = false;
        for (int i = 0; i < NUM_CALIBRATION_POINTS - 1; i++) {
            if (sensorVoltage >= calTable[i].voltage && sensorVoltage <= calTable[i+1].voltage) {
                // Found the segment where our voltage lies
                float x0 = calTable[i].voltage;
                float x1 = calTable[i+1].voltage;
                float y0 = calTable[i].pressure;
                float y1 = calTable[i+1].pressure;
                
                // Linear interpolation
                currentPressure = y0 + (sensorVoltage - x0) * (y1 - y0) / (x1 - x0);
                pointFound = true;
                break;
            }
        }
        
        // Handle out-of-range values
        if (!pointFound) {
            if (sensorVoltage < calTable[0].voltage) {
                // Below minimum voltage - use first point
                currentPressure = calTable[0].pressure;
            } else {
                // Above maximum voltage - use last point
                currentPressure = calTable[NUM_CALIBRATION_POINTS-1].pressure;
            }
        }
        
        // Calculate alpha based on time since last update for consistent smoothing
        if (firstReading) {
            alpha = 1.0f;  // No smoothing on first reading
            firstReading = false;
        } else {
            float elapsed = (currentTime - lastReadTime) / 1000.0f;  // Convert to seconds
            alpha = 1.0f - exp(-elapsed / HALF_LIFE * log(2.0f));
        }
        
        // Apply exponential moving average filter
        smoothedPressure = alpha * currentPressure + (1.0f - alpha) * smoothedPressure;
        
        // Update last read time
        lastReadTime = currentTime;
        lastPressureUpdate = currentTime;
        
        // Debug output
        Serial.print("Raw ADC: ");
        Serial.print(rawADCValue);
        Serial.print(", Voltage: ");
        Serial.print(sensorVoltage, 3);
        Serial.print("V, Pressure: ");
        Serial.print(currentPressure, 3);
        Serial.print(" bar, Smoothed: ");
        Serial.print(smoothedPressure, 3);
        Serial.print(" bar, Alpha: ");
        Serial.println(alpha, 4);
    }
    
    return smoothedPressure;
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
    
    digitalWrite(RELAY_PIN, HIGH);  // Activate relay
    digitalWrite(LED_PIN, LOW);    // Turn LED ON (inverse logic on NodeMCU)
    
    // Log the backflush event
    backflushLogger->logEvent(backflushTriggerPressure, backflushDuration, currentBackflushType);
    pressureLogger->addReading(backflushTriggerPressure, true);
    
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
      digitalWrite(RELAY_PIN, LOW);  // Deactivate relay
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
