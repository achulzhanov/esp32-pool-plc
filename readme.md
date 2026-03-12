# ESP32-S3 Smart Pool Controller

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

An open-source, custom-built hardware and software solution for intelligent pool automation. Designed around the robust Espressif ESP32-S3 microarchitecture, this controller safely orchestrates high-voltage pool pumps, low-voltage valve actuators, standard pool heaters, and environmental temperature monitoring.

## Purpose & Autonomy
This project replaces expensive, proprietary pool automation panels with a modern, easily maintainable, and fully encapsulated C++ Object-Oriented architecture. It safely isolates the low-voltage logic from the high-voltage mains, utilizing a dedicated 24VDC rail for heavy-duty relay coils and a 24VAC rail for standard pool industry actuators.

**Standalone PLC with Home Assistant Integration:**
This board operates as a completely autonomous state machine. Thanks to the hardware DS3231 Real-Time Clock (RTC), it executes all daily scheduling, timeout protocols, freeze-protection logic, and safety interlocks completely locally. It requires zero cloud connectivity or active network intervention to operate safely.

While it integrates natively with **Home Assistant** using MQTT, it intentionally does *not* expose raw relay or actuator states. Instead, it exposes a high-level UI facade: users simply select "Pool Mode" or "Spa Mode," set target temperatures, or toggle features like lights and spa jets. The PLC securely manages the underlying valve sequencing, relay delays, and safety dependencies (e.g., ensuring the pump is primed before the heater ignites) completely under the hood. Context-aware water temperature is reported based on the system's current active mode.

## Capabilities
* **High-Level Home Assistant API:** Exposes clean, user-friendly operational modes while abstracting away dangerous electromechanical states.
* **Internal Safety Interlocks:** Hard-coded logic prevents equipment failure, such as dry-firing the heater or deadheading the plumbing. 
* **Dedicated Web GUI (Planned):** A separate, localized web interface strictly for deep system configuration, schedule management, and manual hardware overrides for maintenance.
* **5x High-Voltage Circuits:** Controls 120VAC or 240VAC heavy loads (e.g., primary filtration pumps, blower fans, booster pumps, and pool lights).
* **2x Valve Actuators:** Standard 24VAC forward/reverse control for pool/spa return and suction diverter valves.
* **1x Heater Igniter Control:** Isolated dry-contact or 24VAC trigger for standard gas pool heaters.
* **Water Temperature Monitoring:** Reads an OEM-style 10k NTC analog thermistor plumbed directly into the PVC manifold.
* **Ambient Air Monitoring:** Reads a waterproof DS18B20 digital 1-Wire sensor for freeze-protection and environmental logic.

## Hardware Bill of Materials (BOM)
### Logic Controller
* **KinCony KC868-A8v3 (ESP32-S3):** The main PLC. Utilizes its onboard PCF8575 I2C expander to drive 8 localized relays, which act as the pilot switches for the heavy external hardware. Also houses the onboard DS3231 RTC for offline scheduling.
* **Custom Sensor Breakout Board:** A perfboard circuit managing the 3.3V power distribution, digital decoupling capacitors, and the 10kΩ pull-up voltage divider required for the analog water thermistor.

### Low Voltage Power & Switching
* **24VDC Power Supply:** Provides clean DC power to the KinCony PLC and drives the coils of the high-voltage relays.
* **5x Omron G7L DPST 25A Relays (24VDC Coils):** Industrial-grade contactors responsible for physically switching the 120V/240V AC loads. Driven safely via the KinCony's onboard relays.
* **TIB100A 24VAC 96VA Transformer:** A heavy-duty step-down transformer dedicated strictly to powering the pool valve actuators.

## Software Architecture
This project is built using **PlatformIO** and modern C++ principles. The hardware intricacies (I2C bus shifting, ADC conversion, bit-masking, and RTC polling) are strictly abstracted and encapsulated within dedicated class objects, leaving the main program loop exceptionally clean and fault-tolerant.

*(Note: Full wiring schematics, PCB layouts, and physical enclosure diagrams will be added to this repository in a future update).*

---
**License:** This project is licensed under the Apache 2.0 License. See the `LICENSE` file for details.