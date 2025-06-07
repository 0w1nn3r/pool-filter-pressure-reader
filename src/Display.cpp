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
      scheduler(nullptr),
      lastOtaFlashTime(0),
      lastDisplayToggleTime(0),
      showOtaText(false),
      showThreshold(true) {
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
  
  // Handle display toggling between threshold and next scheduled backflush
  if (millis() - lastDisplayToggleTime >= 5000) { // Toggle every 5 seconds
    showThreshold = !showThreshold;
    lastDisplayToggleTime = millis();
  }
  
  // Display backflush status at bottom left
  display.setTextSize(1);
  display.setCursor(0, 56);
  
  if (backflushActive) {
    // Show backflush progress
    unsigned long elapsedTime = (millis() - backflushStartTime) / 1000;
    display.print(F("BACKFLUSH: "));
    display.print(elapsedTime);
    display.print(F("/"));
    display.print(backflushDuration);
    display.print(F("s"));
  } else if (showThreshold) {
    // Show threshold
    display.print(F("Threshold: "));
    display.print(backflushThreshold, 1);
    display.print(F(" bar"));
  } else if (scheduler) {
    // Show next scheduled backflush time
    time_t nextTime;
    unsigned int duration;
    if (scheduler->getNextScheduledTime(nextTime, duration)) {
      char timeStr[16];
      struct tm *timeinfo = localtime(&nextTime);
      snprintf(timeStr, sizeof(timeStr), "%d/%d@%d:%d", 
          timeinfo->tm_mday, 
          timeinfo->tm_mon + 1, 
          timeinfo->tm_hour, 
          timeinfo->tm_min);
      
      // Format: "Next DD/M@HH:MM"
      char displayStr[20];
      snprintf(displayStr, sizeof(displayStr), "Next %s", timeStr);
      
      // Calculate position to center the text
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(displayStr, 0, 0, &x1, &y1, &w, &h);
      int xPos = (display.width() - w) / 2; // Center the text
      
      display.setCursor(xPos, 56);
      display.print(displayStr);
    } else {
      // No scheduled backflush
      display.print(F("No scheduled backflush"));
    }
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
  if (!displayAvailable) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(message);
  display.print(F("Restarting in "));
  display.print(countdownSeconds);
  display.println(F("s"));
  display.display();
}

void Display::showMessage(const String& title, const String& message) {
  if (!displayAvailable) return;
  
  display.clearDisplay();
  
  // Display title in larger text
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(title);
  
  // Draw a line under the title
  display.drawLine(0, 10, display.width(), 10, SSD1306_WHITE);
  
  // Display message in normal text below the line
  display.setCursor(0, 15);
  
  // Split message into lines at newline characters
  int lastPos = 0;
  int newlinePos;
  int yPos = 15;
  String line;
  
  do {
    newlinePos = message.indexOf('\n', lastPos);
    if (newlinePos == -1) {
      line = message.substring(lastPos);
    } else {
      line = message.substring(lastPos, newlinePos);
      lastPos = newlinePos + 1;
    }
    
    // Handle long lines by wrapping text
    while (line.length() > 0) {
      int charsToPrint = min(21, (int)line.length()); // 21 chars per line for 128px width
      display.setCursor(0, yPos);
      display.println(line.substring(0, charsToPrint));
      yPos += 10; // 8px font height + 2px spacing
      line = line.substring(charsToPrint);
      
      // Don't overflow the display
      if (yPos >= display.height()) {
        break;
      }
    }
    
    if (yPos >= display.height()) {
      break;
    }
  } while (newlinePos != -1);
  
  display.display();
}
