#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <ESP8266WebServer.h>

class WebServer {
private:
    ESP8266WebServer server;
    float& currentPressure;
    float& backflushThreshold;
    unsigned int& backflushDuration;
    bool& backflushActive;
    unsigned long& backflushStartTime;
    bool& backflushConfigChanged;

    void handleRoot();
    void handleAPI();
    void handleBackflushConfig();

public:
    WebServer(float& pressure, float& threshold, unsigned int& duration, 
              bool& active, unsigned long& startTime, bool& configChanged);
    void begin();
    void handleClient();
};

#endif // WEBSERVER_H
