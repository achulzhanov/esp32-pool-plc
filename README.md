# ESP32-S3 Smart Pool Controller

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

An open-source, custom-built hardware and software solution for intelligent pool automation. Designed around the robust Espressif ESP32-S3 microarchitecture, this controller safely orchestrates high-voltage pool pumps, low-voltage valve actuators, standard pool heaters, and environmental temperature monitoring.

## Purpose & Autonomy
This project replaces expensive, proprietary pool automation panels with a modern, easily maintainable, and fully encapsulated C++ Object-Oriented architecture. It safely isolates the low-voltage logic from the high-voltage mains, utilizing a dedicated 24VDC rail for heavy-duty relay coils and a 24VAC rail for standard pool industry actuators.

**Standalone PLC:**
This board operates as a completely autonomous state machine. Thanks to the hardware DS3231 Real-Time Clock (RTC), it executes all daily scheduling, timeout protocols, freeze-protection logic, and safety interlocks completely locally. It requires zero cloud connectivity or active network intervention to operate safely.

**Dual-Layer Control Architecture:**
To separate daily use from deep system maintenance, the controller features two distinct communication layers:
* **The Admin Layer (Web GUI & REST API):** A localized, zero-RAM footprint web interface hosted directly on the ESP32. This layer is strictly for initial network setup, deep system configuration, automation scheduling, and safely isolating equipment into "Service Mode" for physical maintenance.
* **The User Layer (MQTT & Home Assistant):** Integrates natively with Home Assistant using MQTT Auto-Discovery. It intentionally does *not* expose raw relay or actuator states. Instead, it exposes a high-level UI facade: users simply select "Pool Mode" or "Spa Mode," set target temperatures, or toggle features. The PLC securely manages the underlying valve sequencing, relay delays, and safety dependencies completely under the hood.

## Capabilities
* **High-Level Home Assistant API:** Exposes clean, user-friendly operational modes via MQTT while abstracting away dangerous electromechanical states.
* **Internal Safety Interlocks:** Hard-coded logic prevents equipment failure, such as dry-firing the heater or deadheading the plumbing. 
* **Smart Cancel Overrides:** Manual toggles intelligently suppress or release active schedules based on highly specific timeout parameters.
* **5x High-Voltage Circuits:** Controls 120VAC or 240VAC heavy loads (e.g., primary filtration pumps, blower fans, booster pumps, and pool lights).
* **2x Valve Actuators:** Standard 24VAC forward/reverse control for pool/spa return and suction diverter valves.
* **1x Heater Igniter Control:** Isolated dry-contact or 24VAC trigger for standard gas pool heaters.
* **Water Temperature Monitoring:** Reads an OEM-style 10k NTC analog thermistor plumbed directly into the PVC manifold.
* **Ambient Air Monitoring:** Reads a waterproof DS18B20 digital 1-Wire sensor for freeze-protection and environmental logic.

---

## Getting Started

### 1. Prerequisites
To compile and flash this firmware, you will need:
* **VS Code** with the **PlatformIO** extension installed.
* A **KinCony KC868-A8v3** ESP32-S3 board.
* A USB-C cable for flashing.

### 2. Compilation & Flashing
1. Clone this repository to your local machine.
2. Open the project folder in VS Code. PlatformIO will automatically detect the `platformio.ini` file and download the required toolchains and libraries (PubSubClient, ArduinoJson, RTClib, etc.).
3. Connect your KinCony board via USB-C.
4. Click the **PlatformIO: Upload** arrow icon in the bottom status bar to compile and flash the firmware.

### 3. Initial Wi-Fi Setup (Captive Portal)
1. Power up the board. Because there are no saved credentials on a fresh flash, the system bypasses the normal boot grace period and instantly enters AP Mode.
2. Look for a new Wi-Fi network named **PoolControllerSetup** and connect to it using your phone or laptop.
3. A captive portal should appear automatically. If it doesn't, navigate to `http://192.168.4.1` in your browser.
4. Enter your home network's SSID and Password and click **Save**. The board will save these to NVS memory, reboot, and connect to your local network.

*(Note: If the network drops during normal operation later on, the board will NOT return to AP mode. It will silently attempt to reconnect in the background while safely maintaining physical pool operations. AP mode is only triggered if credentials are wiped or if the router cannot be found during a 5-minute boot grace period).*

### 4. System & MQTT Configuration
Once the board is connected to your router, you need to configure the MQTT transport layer so it can talk to Home Assistant.
1. Find the IP address assigned to the KinCony board by your router (e.g., via your router's admin page).
2. Open a web browser and navigate to that IP address to load the built-in **Web Admin Dashboard**.
3. Click on the **Network** tab.
4. Under the **MQTT Configuration** section, enter the IP address and Port (default `1883`) of your Home Assistant MQTT Broker.
5. Click **Update MQTT & Reboot**.

### 5. Home Assistant Integration
This controller utilizes MQTT Auto-Discovery. You do not need to edit any YAML files.
1. Once the board reboots with the MQTT IP saved, open your Home Assistant dashboard.
2. Go to **Settings > Devices & Services > MQTT > Devices**.
3. You will see a newly discovered device named **Pool Controller**.
4. Click on it to view all available switches, timeout sliders, sensors, and the dual (Pool/Spa) thermostats.
5. Click **Add to Dashboard** to begin building your custom UI.

---

## Hardware Bill of Materials (BOM)
### Logic Controller
* **KinCony KC868-A8v3 (ESP32-S3):** The main PLC. Utilizes its onboard PCF8575 I2C expander to drive 8 localized relays, which act as the pilot switches for the heavy external hardware. Also houses the onboard DS3231 RTC for offline scheduling.
* **Custom Sensor Breakout Board:** A perfboard circuit managing the 3.3V power distribution, digital decoupling capacitors, and the 10kΩ pull-up voltage divider required for the analog water thermistor.

### Power & Switching
* **24VDC Power Supply:** Provides clean DC power to the KinCony PLC and drives the coils of the high-voltage relays.
* **5x Omron G7L DPST 25A Relays (24VDC Coils):** Industrial-grade contactors responsible for physically switching the 120V/240V AC loads. Driven safely via the KinCony's onboard relays.
* **TIB100A 24VAC 96VA Transformer:** A heavy-duty step-down transformer dedicated strictly to powering the pool valve actuators.

## Software Architecture
This project is built using **PlatformIO** and modern C++ principles. The hardware intricacies (I2C bus shifting, ADC conversion, bit-masking, and RTC polling) are strictly abstracted and encapsulated within dedicated class objects, leaving the main program loop exceptionally clean and fault-tolerant. 

For a deep dive into the software architecture, dependency injection chain, and memory management strategies, see the [Architecture Documentation in the `include/` directory](include/README.md).

*(Note: Full wiring schematics, PCB layouts, and physical enclosure diagrams will be added to this repository in a future update).*

---
**License:** This project is licensed under the Apache 2.0 License. See the `LICENSE` file for details.