# Smart Building Access and Monitoring System

A coursework-style ESP32 project that combines access control, time-based entry rules, room occupancy monitoring, and automated lighting for a smart building environment.

The system is built with ESP-IDF and targets an ESP32 DevKit C V4. It uses RFID card authentication, PIR motion sensors, LDR light sensors, an RTC module, an SD card logger, and simple visual/audible feedback to simulate a modern smart-building control panel.

## Overview

This project implements a complete building access workflow:

- Two MFRC522 RFID readers monitor entry and exit points.
- A predefined set of authorized users is stored in firmware.
- Entry is restricted to a configured daily access schedule.
- A blue administrator card allows temporary schedule changes and admin session control.
- Occupancy is tracked as users enter and exit.
- PIR sensors and LDRs automatically switch room lights on or off based on motion and ambient light.
- RTC and SD card modules provide real-time control and persistent logging.
- A serial menu enables manual status checks and schedule management.

## Features

### Access control
- RFID-based access for authorized users only
- Separate entry and exit RFID readers
- Duplicate entry prevention
- Safe egress even outside schedule hours
- Admin authentication via a designated blue RFID card
- Schedule enforcement with default hours 07:00 to 18:00
- Real-time access attempts, grant/deny statistics, and occupancy counts

### Building monitoring
- Occupancy tracking for users inside the building
- Dashboard display for building status, room states, and door lock state
- Registered user listing and current inside/outside status
- Motion detection using PIR sensors
- LDR-based light automation for room brightness conditions
- Automatic room light timeout when motion ends

### Data and logging
- SD card mounted at `/sdcard`
- CSV log file: `ACCESS.csv`
- Schedule persistence file: `SCHEDULE.txt`
- Event logging for access attempts, occupancy changes, admin actions, and lighting events

### Feedback systems
- Green LED for successful access
- Red LED for denied access
- Buzzer feedback for granted/denied events
- Serial terminal dashboard and menu system

## Hardware Architecture

The project is designed around an ESP32 and the following external modules:

- 2x MFRC522 RFID readers
- 2x PIR motion sensors
- 2x LDR sensors (photoresistors)
- 1x DS1307 RTC module
- 1x microSD card module
- 2x room lights
- 1x buzzer
- 2x status LEDs
- Door simulation logic using the door lock module

The wiring is defined in `diagram.json` and the simulator configuration is in `wokwi.toml`.

## Project Structure

```text
.
├── CMakeLists.txt
├── diagram.json
├── wokwi.toml
├── sdkconfig
├── build/
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── access_control.c
│   ├── access_feedback.c
│   ├── door_lock.c
│   ├── monitoring.c
│   ├── rooms.c
│   ├── rtc.c
│   ├── sd_logger.c
│   └── mfrc522.c
└── README.md
```

### Main module responsibilities

- `main.c`  
  Bootstraps the system, initializes all modules, starts the serial command menu, and runs the main control loop.

- `access_control.c`  
  Handles RFID user validation, admin authentication, access scheduling, occupancy changes, and denied/granted access logic.

- `access_feedback.c`  
  Controls the status LED and buzzer for visual/audible feedback.

- `door_lock.c`  
  Simulates the door lock state for a temporary unlock window.

- `rooms.c`  
  Handles PIR interrupts, light automation, and LDR-based room control.

- `monitoring.c`  
  Prints the smart-building dashboard at intervals.

- `rtc.c`  
  Initializes and reads the DS1307 real-time clock over I2C.

- `sd_logger.c`  
  Mounts the SD card and logs events and access schedules to persistent files.

- `mfrc522.c`  
  Implements the shared SPI bus and communication logic for the MFRC522 readers.

## Serial Command Menu

The application exposes a serial menu in the terminal. The available actions are:

| Command | Action |
| --- | --- |
| 1 | View SD card access logs |
| 2 | View system dashboard |
| 3 | View registered users |
| 4 | View current building occupancy |
| 5 | Display menu |
| 6 | View current access schedule |
| 7 | Change access schedule (Admin only) |
| 8 | Administrator logout |

## Default Users

The application contains a built-in authorized user set:

- User 1
- User 2 (blue card; administrator)
- User 3
- User 4

The admin session is valid for 5 minutes after scanning the blue card, and the schedule can be changed only while that session is active.

## Access Schedule Behavior

- Default schedule: 07:00 to 18:00
- Entry is allowed only within the configured time window
- Exit is always permitted for safety
- The schedule can be saved to SD card and restored automatically on startup

## Getting Started

### Prerequisites

- ESP-IDF installed and configured
- An ESP32-compatible toolchain
- USB serial connection for flashing and monitoring
- Optional: Wokwi for simulation

### Build

From the project root:

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

### Monitor

```bash
idf.py monitor
```

### Wokwi simulation

The project includes `wokwi.toml` and `diagram.json`, making it easy to run in Wokwi when configured for the workspace.

## Notes

- The firmware logs access, occupancy, room activity, and schedule updates to the SD card if available.
- If the SD card is not mounted, the system continues in a reduced-capability mode but still runs the access and monitoring logic.
- RTC access is essential for schedule validation; if the clock is unavailable, entry is denied until the RTC is initialized correctly.

## Summary

This project demonstrates a practical smart-building control design using ESP32 hardware and embedded C. It combines security, automation, monitoring, and persistence into a single firmware application suitable for coursework, prototyping, and embedded-systems learning.
