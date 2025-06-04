# Pool Filter Pressure Reader

A pressure monitoring system for pool filters using a NodeMCU ESP8266 and an analog pressure sensor.

## Features

- Real-time pressure monitoring with analog pressure sensor
- OLED display showing current pressure and WiFi status (optional - system works without display)
- Web interface for remote monitoring with visual pressure gauge
- Automatic backflush control with configurable threshold and duration
- Backflush event logging with timestamps and pressure readings
- NTP time synchronization for accurate timestamps
- Pressure history logging with graphical display
- Automatic space management to prevent running out of storage
- WiFi settings management directly from the web interface
- Persistent settings storage using the EEPROM
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
- One terminal: D7 (GPIO13)
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
| WiFi:[||| ]  IP:2.42       |
|                            |
|                            |
|         1.4     bar        |
|                            |
|                            |
| Threshold: 2.0 bar         |
+----------------------------+
```

The top row shows WiFi signal strength and the last two octets of the IP address. The center shows the current pressure reading in bar with large digits for easy reading. The bottom row shows either the backflush threshold or active backflush status with countdown.

### Resetting All Settings

1. Press and hold the reset button while powering on the device
2. The display will show a countdown 
3. Release the button after countdown ends and the device will clear all settings (WiFi credentials, backflush configuration, stored history, etc.)
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
- Current time display (synchronized via NTP and with timezone via IP geolocation)
- Backflush configuration settings
- Manual backflush trigger button
- Links to backflush event log, pressure history, sensor settings,and WiFi settings

### Backflush Log Page
- Table of all backflush events with date, time, pressure, and duration
- Option to clear the log history
- Navigation back to the main dashboard

### Pressure History Page
- Interactive graph showing pressure readings over time
- Zoom in and out
- Automatic recording of significant pressure changes (0.1 bar or more)
- Option to clear pressure history
- Option to set auto prune age
- Option to export pressure history as CSV
- Navigation between dashboard and backflush log

### WiFi Settings Page
- Display current WiFi network information (SSID, IP address, signal strength)
- Option to reset WiFi settings and enter configuration mode
- Automatically creates an access point for new WiFi setup

## API Endpoints

- `/` - Main web interface with pressure gauge and backflush configuration
- `/api` - JSON API with current status and settings
- `/backflush` - Configure backflush settings
- `/log` - Backflush event log page with timestamp and pressure data
- `/clearlog` - Clear the backflush event log (redirects to log page)
- `/pressure` - Pressure history page with interactive graph
- `/clearpressure` - Clear the pressure history data (redirects to pressure page)
- `/wifi` - WiFi settings page to scan/select new WiFi networks, connect, or reset current configuration.
- `/manualbackflush` - POST endpoint to trigger a manual backflush operation
- `/stopbackflush` - POST endpoint to stop an active backflush
- `/settings` - Device settings page for sensor configuration and software updates
- `/ota` - POST endpoint to enable OTA updates
- `/sensorconfig` - POST endpoint to update pressure sensor configuration
- `/setretention` - POST endpoint to configure data retention settings
- `/pressure.csv` - Download pressure history data in CSV format

## Over-The-Air Updates

The device supports Over-The-Air (OTA) firmware updates, allowing you to update the firmware without physically connecting to the device. Here's how to perform an update:

### For End Users

1. Download the latest firmware file (`.bin`) from the project releases page
2. Connect your computer to the same WiFi network as your pool filter device
3. Open the pool filter web interface in your browser (usually at http://pool-filter.local)
4. Go to the Settings page
5. Click the "Enable OTA Updates" button
6. Use one of these methods to upload the firmware within 5 minutes:

   #### Using ESP8266 Flasher Tool (Windows)
   - Download [ESP8266 Flasher](https://github.com/nodemcu/nodemcu-flasher)
   - Run the application
   - Select the downloaded firmware file
   - Set the address to `pool-filter.local`
   - Click Flash

   #### Using espota.py (Advanced Users)
   ```bash
   python3 espota.py -i pool-filter.local -p 8266 -f firmware.bin
   ```

### For Developers

If you're building from source:

1. Set up PlatformIO
2. Configure `platformio.ini` with:
   ```ini
   upload_protocol = espota
   upload_port = pool-filter.local
   ```
3. Enable OTA updates in the web interface
4. Run `pio run -t upload`

For security, the OTA update window is limited to 5 minutes. After this time, OTA updates will be disabled until re-enabled through the web interface.

### Troubleshooting

- Make sure your computer and the device are on the same WiFi network
- If `pool-filter.local` doesn't work, use the device's IP address (shown in the Settings page)
- If the update fails, try power cycling the device and starting over
- The LED will blink during the update - don't power off the device during this time

## Future Improvements

- Control of circulation pump
- Implement MQTT support for IoT integration
- Email or push notifications for backflush events
- Mobile app integration
- Predictive maintenance alerts based on pressure trends

## Screenshots

<img width="819" alt="Screenshot 2025-05-31 at 10 48 08" src="https://github.com/user-attachments/assets/6109c096-1ad7-4dc5-9fb5-ca21ab407440" />

<img width="1381" alt="Screenshot 2025-05-31 at 18 24 52" src="https://github.com/user-attachments/assets/758b265a-4bcf-4d9f-94f2-9a7130f6151f" />
