# Pool Filter Pressure Reader

A pressure monitoring system for pool filters using a NodeMCU ESP8266 and an analog pressure sensor.

## Features

- Real-time pressure monitoring with analog pressure sensor
- OLED display showing current pressure and WiFi status (optional - system works without display)
- Web interface for remote monitoring with visual pressure gauge
- Automatic backflush control with configurable threshold and duration
- Scheduled backflush operations with support for multiple schedules
- Backflush event logging with timestamps and pressure readings
- NTP time synchronization and geo based time zone detection for accurate timestamps
- Pressure history logging with graphical display
- Automatic space management to prevent running out of storage
- WiFi settings management directly from the web interface
- Persistent settings storage using the flash filesystem
- JSON API endpoint for integration with other systems
- WiFi configuration portal for easy setup
- Web based firmware upgrade facility
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

## Web Interface

### Main Interface (`/`)
- Real-time pressure gauge display
- Current backflush configuration
- System status and sensor readings
- Navigation to all other sections

### Backflush Management
- `/backflush` - Configure backflush settings (threshold, duration, type)
- `/manualbackflush` (POST) - Trigger a manual backflush operation
- `/stopbackflush` (POST) - Stop an active backflush
- `/log` - View backflush event history
- `/clearlog` - Clear the backflush event log

### Pressure Monitoring
- `/pressure` - Interactive pressure history graph
- `/pressure.csv` - Download pressure history in CSV format
- `/clearpressure` - Clear pressure history data

### System Configuration
- `/settings` - Device settings and status
- `/sensorconfig` (POST) - Update pressure sensor configuration
- `/setretention` (POST) - Configure data retention settings
- `/wifi` - WiFi network configuration
  - Scan for available networks
  - Connect to new networks
  - Reset WiFi configuration

### Scheduling
- `/schedule` - Manage automated backflush schedules
- `/scheduleupdate` (POST) - Add or update a schedule
- `/scheduledelete` (POST) - Remove a schedule

### System Updates
- `/ota` (POST) - Enable OTA update mode
- `/otaupload` - Web-based firmware upload interface

### API Endpoints
- `/api` - JSON API with current status and sensor readings
  - Returns pressure, voltage, backflush status, and system info
  - Can be used for integration with home automation systems

## Over-The-Air Updates

The device supports multiple methods for Over-The-Air (OTA) firmware updates:

### Web-Based Update
1. Navigate to the `/ota` page
2. Click "Enable OTA Update"
3. Use the web form to upload the new firmware file
4. The device will automatically reboot after successful update

### Using Arduino IDE
1. Navigate to the `/ota` page and click "Enable OTA Update"
2. In Arduino IDE, select "Tools" > "Port" > "[device_name] at [IP]"
3. Select "Sketch" > "Upload"

### Using PlatformIO
1. Navigate to the `/ota` page and click "Enable OTA Update"
2. Update your `platformio.ini` with the device's IP address:
   ```ini
   upload_protocol = espota
   upload_port = [device_ip]  # or use the device's .local address (e.g., pool-filter.local)
   ```
3. Run `pio run -t upload`

### Update Notes
- OTA updates are enabled for 5 minutes after activation
- The device will automatically disable OTA updates after the timeout period
- Current firmware version and update status are shown on the settings page
- The device will automatically reboot after a successful update

### Alternative Methods (Advanced)

#### Using ESP8266 Flasher Tool (Windows)
1. Download [ESP8266 Flasher](https://github.com/nodemcu/nodemcu-flasher)
2. Run the application
3. Select the downloaded firmware file
4. Set the address to `pool-filter.local` or the device's IP
5. Click Flash

#### Using espota.py
```bash
python3 espota.py -i pool-filter.local -p 8266 -f firmware.bin
```

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
