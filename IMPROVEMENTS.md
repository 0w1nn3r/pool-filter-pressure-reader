# Pool Filter System Improvements

## 1. Storage and Streaming Improvements

### Problem
Current implementation stores all pressure readings in memory, limiting the history to approximately 15KB of data. This document outlines a plan to store and efficiently access larger datasets using flash storage.

## Problem
Current implementation stores all pressure readings in memory, limiting the history to approximately 15KB of data. This document outlines a plan to store and efficiently access larger datasets using flash storage.

### Solution Architecture

#### 1.1 File-based Storage
- Implement circular buffer storage on the filesystem (LittleFS/SPIFFS)
- Use fixed-size records (timestamp + pressure value) for efficient random access
- Maintain an in-memory index of file positions for quick seeking

#### 1.2 Data Access API
- Modify API to support pagination with `start_time` and `limit` parameters
- Implement binary search for efficient time-based lookups in the data file
- Stream data directly from filesystem to network to minimize memory usage

#### 1.3 Memory Management
- Small in-memory cache for recent readings (last N points)
- Fixed-size read buffer (e.g., 1KB) for filesystem operations
- Streaming response implementation to avoid loading entire datasets

#### 1.4 File Management
- Implement file rotation when reaching maximum file size (e.g., 1MB)
- Maintain a manifest file tracking all data files and their time ranges
- Optional compression for older data files

#### 1.5 Real-time Updates
- WebSocket implementation for pushing new readings in real-time
- Separate channels for historical data and live updates

#### 1.6 Performance Optimization
- Cache frequently accessed time ranges in memory
- Pre-compute and store aggregate statistics (min/max/avg) for common time windows
- Implement data downsampling for very old data

#### 1.7 Data Retention
- Configurable retention policies based on age or storage limits
- Automatic cleanup of old data
- Optional archiving of historical data before deletion

### 1.8 Implementation Considerations
- Ensure thread safety for concurrent filesystem and network operations
- Implement proper error handling for filesystem operations
- Add monitoring for filesystem usage and health
- Consider power-loss protection for data integrity

### 1.9 Expected Benefits
- Virtually unlimited storage capacity (within flash limits)
- Efficient memory usage regardless of dataset size
- Fast access to both recent and historical data
- Better scalability for long-term monitoring

## 2. Circulation Pump Control

### 2.1 Hardware Requirements
- Additional relay module for pump control
- Proper circuit protection and isolation
- Status LED/indicator

### 2.2 Software Components

#### 2.2.1 Pump Controller
- Relay control interface
- State management (ON/OFF)
- Manual override functionality
- Safety interlocks (e.g., max runtime, thermal protection)

#### 2.2.2 Schedule Management
- Daily scheduling similar to backflush scheduler
- Multiple time slots per day
- Weekday/weekend differentiation
- Holiday mode support

#### 2.2.3 Web Interface
- Manual ON/OFF controls
- Current status display
- Schedule management UI
- Runtime statistics

#### 2.2.4 Data Logging
- State change logging (ON/OFF events)
- Runtime tracking
- Power consumption estimation
- Fault logging

### 2.3 Visualization
- Add second series to pressure graph showing pump state
- Color-coded pump state indicators
- Historical state timeline
- Runtime statistics and reports

### 2.4 API Endpoints
- `GET /api/pump/state` - Get current state
- `POST /api/pump/control` - Manual control
- `GET /api/pump/schedule` - Get schedule
- `POST /api/pump/schedule` - Update schedule
- `GET /api/pump/history` - State history

### 2.5 Safety Features
- Minimum ON/OFF times
- Overcurrent protection
- Watchdog timer
- Manual override capability
- Status reporting

### 2.6 Implementation Considerations
- Ensure safe default states on power-up
- Add proper error handling for relay operations
- Implement state persistence
- Consider power consumption monitoring
- Add remote alerting for critical failures

## 3. Improved Sensor Reading with External ADC (ADS1015)

### 3.1 Current Limitations
- Built-in ADC may have noise and stability issues
- Limited resolution (10-12 bits typically)
- Susceptible to power supply fluctuations
- Potential ground loop issues

### 3.2 ADS1015 Benefits
- 12-bit resolution (4.096V full-scale range)
- Programmable gain amplifier (PGA)
- I²C interface
- Internal voltage reference
- 4 differential/8 single-ended inputs
- Up to 3300 samples per second

### 3.3 Hardware Implementation
- Connect ADS1015 to I²C bus
- Power supply decoupling
- Proper signal conditioning for pressure sensor
- Shielding for noise reduction
- Optional voltage reference circuit

### 3.4 Software Implementation

#### 3.4.1 Driver Integration
- Use Adafruit_ADS1X15 library
- Implement proper I²C initialization
- Configure gain and data rate
- Add error handling for I2C communication

#### 3.4.2 Signal Processing
- Implement moving average filter
- Add digital low-pass filtering
- Detect and handle sensor disconnections
- Automatic calibration routine

#### 3.4.3 Calibration
- Two-point calibration (zero and full-scale)
- Store calibration parameters in EEPROM
- Web interface for calibration
- Temperature compensation if needed

### 3.5 Performance Improvements
- Higher resolution readings
- Better noise immunity
- More stable baseline
- Improved accuracy at low pressures

### 3.6 Testing and Validation
- Compare with reference pressure gauge
- Long-term stability testing
- Temperature variation testing
- Noise floor measurement

### 3.7 Web Interface Updates
- Real-time ADC readings display
- Raw vs. filtered data visualization
- Calibration interface
- Diagnostic information

### 3.8 Safety Considerations
- Input voltage protection
- Current limiting
- Proper grounding
- ESD protection

### 3.9 Expected Outcomes
- More accurate pressure readings
- Reduced reading fluctuations
- Better resolution for small pressure changes
- More reliable operation in various conditions
