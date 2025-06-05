#include "Display.h"
#include "WebServer.h"

Display::Display(Adafruit_SSD1306& oled, float& pressure, float& threshold, 
                 unsigned int& duration, bool& active, unsigned long& startTime, TimeManager* tm)
    : display(oled),
      currentPressure(pressure),
      backflushThreshold(threshold),
      backflushDuration(duration),
      backflushActive(active),
      backflushStartTime(startTime),
      displayAvailable(false),
      timeManager(tm),
      webServer(nullptr),
      lastOtaFlashTime(0),
      showOtaText(false) {
}

bool Display::init() {
  // Try to initialize the display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3C for most 128x64 displays
    Serial.println(F("SSD1306 allocation failed - continuing without display"));
    displayAvailable = false;
    return false;
  }
  
  displayAvailable = true;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  return true;
}

void Display::showStartupScreen() {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Pool Filter"));
  display.println(F("Pressure Reader"));
  display.println(F("Initializing..."));
  display.display();
}

void Display::showWiFiConnecting() {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Connecting to WiFi..."));
  display.display();
}

void Display::showWiFiConnected(String ssid, IPAddress ip) {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("WiFi Connected!"));
  display.print(F("SSID: "));
  display.println(ssid);
  display.print(F("IP: "));
  display.println(ip);
  display.display();
}

void Display::showTimezone() {
  if (!displayAvailable || !timeManager) return;
  
  // Continue from current cursor position
  if (timeManager->isTimeInitialized()) {
    // Show current date and time
    display.println(timeManager->formatDate(timeManager->getCurrentTime()));
    display.println(timeManager->formatTime(timeManager->getCurrentTime()));
    
    // Show timezone
    if (timeManager->isTimezoneInitialized()) {
      int32_t offset = timeManager->getTimezoneOffset();
      int offsetHours = offset / 3600;
      display.print(F("Timezone: GMT"));
      if (offsetHours >= 0) display.print(F("+"));
      display.println(offsetHours);
    } else {
      display.println(F("Timezone: UTC"));
    }
  } else {
    display.println(F("Time not synced"));
  }
  display.display();
  // allow time to read it
  delay(2000);
}

void Display::showWiFiSetupMode(String apName) {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("WiFi Setup Mode"));
  display.println(F("Connect to:"));
  display.println(apName);
  display.println(F("Then go to:"));
  display.println(F("192.168.4.1"));
  display.display();
}

void Display::showResetMessage() {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("RESET BUTTON PRESSED"));
  display.println(F("Clearing settings"));
  display.display();
}

void Display::updateDisplay() {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  
  // Display WiFi status in top row with signal bars
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // WiFi status indicator
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    
    // Draw WiFi status text and signal bars
    display.setCursor(0, 0);
    display.print(F("WiFi:"));
    if (rssi > -55) {
      display.print(F("[||||]"));
    } else if (rssi > -65) {
      display.print(F("[||| ]"));
    } else if (rssi > -75) {
      display.print(F("[||  ]"));
    } else if (rssi > -85) {
      display.print(F("[|   ]"));
    } else {
      display.print(F("[    ]"));
    }
    
    // Show IP address (last two octets)
    display.setCursor(70, 0);
    display.print(F("IP:"));
    display.print(WiFi.localIP()[2]);
    display.print(".");
    display.print(WiFi.localIP()[3]);
  } else {
    display.print(F("WiFi:[X]"));
  }
  
  // Check if OTA mode is active and handle flashing display
  bool otaActive = webServer && webServer->isOTAEnabled();
  
  // Flash between OTA text and pressure reading every 1000ms when OTA is active
  if (otaActive) {
    // Check if it's time to toggle the display
    if (millis() - lastOtaFlashTime >= 1000) {
      showOtaText = !showOtaText;
      lastOtaFlashTime = millis();
    }
    
    if (showOtaText) {
      // Display OTA text in large font in center
      display.setTextSize(3);
      display.setCursor(30, 20);
      display.print(F("OTA"));
    } else {
      // Display pressure in large font in center
      display.setTextSize(3);
      display.setCursor(10, 20);
      display.print(currentPressure, 1);
      
      // Display bar unit
      display.setTextSize(2);
      display.setCursor(90, 30);
      display.print(F("bar"));
    }
  } else {
    // Normal display - just show pressure
    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print(currentPressure, 1);
    
    // Display bar unit
    display.setTextSize(2);
    display.setCursor(90, 30);
    display.print(F("bar"));
  }
  
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

void Display::showFirmwareUpdateProgress(int percentage) {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Updating firmware..."));
  
  // Draw progress bar
  display.drawRect(0, 20, 128, 15, SSD1306_WHITE);
  display.fillRect(2, 22, (int)((percentage / 100.0) * 124), 11, SSD1306_WHITE);
  
  // Display percentage
  display.setTextSize(2);
  display.setCursor(40, 40);
  display.print(percentage);
  display.print(F("%"));
  
  display.display();
}

bool Display::isDisplayAvailable() const {
  return displayAvailable;
}

void Display::showResetCountdown(String message, unsigned int countdownSeconds) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println(message);
  display.println();
  display.setTextSize(2);
  display.print(F("  "));
  display.print(countdownSeconds);
  display.println(F(" sec"));
  display.display();
}