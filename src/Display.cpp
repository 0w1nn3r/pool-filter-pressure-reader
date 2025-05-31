#include "Display.h"

Display::Display(Adafruit_SSD1306& oled, float& pressure, float& threshold, 
                 unsigned int& duration, bool& active, unsigned long& startTime)
    : display(oled),
      currentPressure(pressure),
      backflushThreshold(threshold),
      backflushDuration(duration),
      backflushActive(active),
      backflushStartTime(startTime),
      displayAvailable(false) {
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
  display.println(F("Clearing WiFi settings"));
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

bool Display::isDisplayAvailable() const {
  return displayAvailable;
}
