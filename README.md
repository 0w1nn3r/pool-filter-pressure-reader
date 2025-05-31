# Pool Filter Pressure Reader

A pressure monitoring system for pool filters using a NodeMCU ESP8266 and an analog pressure sensor.

## Features

- Real-time pressure monitoring with analog pressure sensor (displayed in bar)
- Optional OLED display (128x64) showing current pressure with WiFi signal strength indicator
- Headless operation support - can function without OLED display connected
- Web interface for remote monitoring with visual pressure gauge
- Automatic backflush control with configurable pressure threshold and duration
- Backflush event logging with date/time stamps and pressure readings
- Pressure history logging with interactive graph visualization
- Automatic space management to prevent storage overflow
- NTP time synchronization for accurate timestamps
- Persistent settings storage using the Preferences library
- JSON API endpoint for integration with other systems
- WiFi configuration portal for easy setup
- Reset button to clear all settings when needed

## Hardware Requirements

- NodeMCU Lolin V3 Module ESP8266 ESP-12E
- 3-wire analog pressure sensor
- 128x64 OLED display with SSD1315 chip (I2C interface)
- Push button for resetting settings
- Relay module for backflush control
- Internet connection for NTP time synchronization
- Jumper wires
- Power supply (USB or external)

## Wiring

### Pressure Sensor
- Red wire: 3.3V
- Black wire: GND
- Yellow/Signal wire: A0 (Analog input)

### OLED Display (I2C)
- VCC: 3.3V
- GND: GND
- SCL: D1 (GPIO 5)
- SDA: D2 (GPIO 4)

### Reset Button
- One terminal: D3 (GPIO0)
- Other terminal: GND

### Backflush Relay
- Control pin: D5 (GPIO14)
- VCC: External power source appropriate for your relay
- GND: Common ground

## Software Setup

1. Install PlatformIO IDE (VSCode extension or standalone)
2. Clone or download this repository
3. Open the project in PlatformIO
4. Adjust pressure sensor calibration if needed:
   ```cpp
   #define PRESSURE_MIN 0.0   // Minimum pressure in bar
   #define PRESSURE_MAX 4.0   // Maximum pressure in bar
   #define VOLTAGE_MIN 0.5    // Minimum voltage output (V)
   #define VOLTAGE_MAX 4.5    // Maximum voltage output (V)
   ```
5. Build and upload the project to your NodeMCU

## Usage

1. Power on the device
2. On first boot, the device will create an open WiFi access point named "PoolPressure-Setup"
3. Connect to this network (no password required)
4. A configuration portal will automatically open (or navigate to 192.168.4.1)
5. Enter your WiFi credentials in the portal
6. After connecting, the OLED display will show the current pressure and WiFi status
7. The device will automatically synchronize time with an NTP server
8. Access the web interface by navigating to the IP address shown on the OLED display
9. Configure the backflush settings (threshold pressure and duration) on the web interface
10. The device will automatically activate the backflush relay when pressure exceeds the threshold
11. Each backflush event is logged with timestamp and pressure reading
12. View the backflush event log by clicking the "Backflush Log" link on the main page
13. The web page auto-refreshes every 5 seconds
14. For programmatic access, use the `/api` endpoint which returns JSON data

### Display Layout

The OLED display layout looks like this:

```
+----------------------------+
| WiFi:[||||]           IP:42 |
|                            |
|                            |
|         2.4                |
|                 bar        |
|                            |
| Threshold: 2.0 bar         |
+----------------------------+
```

The top row shows WiFi signal strength and the last octet of the IP address. The center shows the current pressure reading in bar with large digits for easy reading. The bottom row shows either the backflush threshold or active backflush status with countdown.

### Resetting All Settings

1. Press and hold the reset button while powering on the device
2. The display will show a reset message
3. Release the button and the device will clear all settings (WiFi credentials, backflush configuration, etc.)
4. The device will restart and enter configuration mode

## Troubleshooting

- If the display shows incorrect pressure readings, check the sensor calibration values
- If WiFi configuration fails, press the reset button while powering on to clear settings
- For analog reading issues, ensure the pressure sensor is properly connected to A0
- If the backflush relay doesn't activate, check the wiring and relay power supply
- If the device is stuck in a reboot loop, try flashing the firmware again
- If no OLED display is connected, the system will continue to function normally - use the web interface for monitoring
- To verify the system is working without a display, check the serial monitor for status messages

## Web Interface

### Main Dashboard
- Real-time pressure display with visual gauge
- Current time display (synchronized via NTP)
- Backflush configuration settings
- Links to backflush event log and pressure history

### Backflush Log Page
- Table of all backflush events with date, time, pressure, and duration
- Option to clear the log history
- Navigation back to the main dashboard

### Pressure History Page
- Interactive graph showing pressure readings over time
- Automatic recording of significant pressure changes (0.1 bar or more)
- Option to clear pressure history
- Navigation between dashboard and backflush log

## API Endpoints

- `/` - Main web interface with pressure gauge and backflush configuration
- `/api` - JSON API with current status and settings
- `/log` - Backflush event log page with timestamp and pressure data
- `/clearlog` - Clear the backflush event log (redirects to log page)
- `/pressure` - Pressure history page with interactive graph
- `/clearpressure` - Clear the pressure history data (redirects to pressure page)
- `/backflush` - POST endpoint to configure backflush settings

## Future Improvements

- Implement alerts for high/low pressure conditions
- Add data export functionality (CSV download)
- Support for multiple pressure sensors
- Add OTA (Over-the-Air) update capability
- Implement MQTT support for IoT integration
- Email or push notifications for backflush events
- Mobile app integration
- Weather data correlation with pressure readings
- Predictive maintenance alerts based on pressure trends
