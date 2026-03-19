# Header & Software Architecture (`include/`)

This directory contains the C++ header files for the ESP32-S3 Smart Pool Controller. The project uses strict Object-Oriented Programming (OOP) to separate concerns into dedicated classes. This isolates the hardware layer from the network stack, the central control logic from the transport protocols, and ensures predictable, crash-free performance over long uptimes.

## Dependency Injection Chain (Initialization Order)

Because this firmware does not use a master configuration header, the order of instantiation in `main.cpp` is critical. Classes must be initialized and passed by reference in a specific order to satisfy their dependencies:

1. **`KinConyPLC` (Hardware):** Initialized first. It claims the I2C/SPI buses and sets the hardware pins to safe default states (OFF) before any logic can execute.
2. **`PoolNetworkManager` (Connectivity):** Initialized second. It mounts the NVS flash memory to retrieve saved credentials and spins up the Ethernet/Wi-Fi stack.
3. **`PoolLogic` (Scheduling, Overrides, Safeties):** Initialized third, requiring references to `KinConyPLC` and `PoolNetworkManager`. It reads the physical sensors, pulls the saved user settings from flash, and takes immediate control of the hardware state machine.
4. **`PoolWebServer` & `PoolMQTT` (The Transports):** Initialized last, requiring references to `PoolNetworkManager` and `PoolLogic`. They act purely as communication interfaces (REST and MQTT) to translate external commands into `PoolLogic` functions.

---

## Architectural Decisions & Class Scopes

If you are modifying this code to fit your specific pool setup, please note the following design constraints driven by the KinCony KC868-A8v3 hardware and the ESP32-S3 RTOS environment:

### `KinConyPLC.h` (Hardware Abstraction)

#### 1. 16-Bit Relay State (`uint16_t _currentRelayState`)
While this board only has 8 physical relays, the variable tracking their state is explicitly defined as a 16-bit integer. The relays are driven by a **PCF8575 I2C I/O Expander**, which is a 16-bit device. Pins `P0-P7` drive the relays, while pins `P8-P15` are wired to the digital inputs. To prevent accidentally overwriting the digital inputs when toggling a relay, the software must read, mask, and transmit a full 16-bit payload during every I2C transaction. 

#### 2. Active-Low Hardware Boundary
The relays driven by the PCF8575 are active-low logic (a `LOW` signal energizes the coil). To keep the high-level logic intuitive (where `true` means ON), the bitwise NOT operator (`~`) is applied exclusively inside the hardware boundary (`writeI2C`) at the exact moment of transmission. This ensures default initializations (e.g., `0x0000`) safely translate to all pins being driven `HIGH` (off) at boot.

#### 3. Strict Type Safety (`enum class PoolRelay : uint8_t`)
To prevent magic numbers and out-of-bounds relay calls, the relays are strictly mapped using an `enum class`. The enum is explicitly packed into a `uint8_t` (1 byte) to conserve RAM.

### `PoolLogic.h` (Equipment Control & Safety)

#### 1. 3-Tier Authority Model (`SystemMode`)
To ensure the system is physically safe to maintain while allowing for home automation flexibility, control authority is strictly tiered:
* **Tier 1: Service Mode (`SERVICE`)** - Reserved for the Web Admin. This mode completely halts the `loop()` state machine. Schedules, timeouts, and thermostats are ignored. Hardware is directly manipulated to safely isolate equipment for maintenance.
* **Tier 2: User Overrides (`USER_OVERRIDE`)** - Reserved for day-to-day user commands via MQTT/Web. These commands are strictly bound by timeouts (e.g., "Turn on Spa for 120 minutes").
* **Tier 3: Automation (`AUTO`)** - The baseline state. Evaluates the RTC clock against user-defined schedules.

#### 2. "Smart Cancel" Override Logic
To prevent manual overrides from fighting with active schedules, `PoolLogic` uses a Smart Cancel system:
* If a user turns an accessory OFF while its schedule is active, the logic creates an explicit "OFF Override" timer to actively suppress the schedule.
* If a user turns an accessory OFF outside of its schedule, the logic instantly zeroes the timer and releases the system back to `AUTO` mode.

#### 3. Failsafes & Safety Interlocks
* **Heater & Vacuum Interlocks:** The gas heater and vacuum booster pump are mathematically forbidden from turning ON unless the main Filter Pump is also commanded ON. 
* **Sensor Failsafe:** If the DS18B20 temperature sensor is disconnected, it returns a massive negative value. The logic actively traps out-of-bounds readings (`< 32F` or `> 110F`) and hard-locks the heater from firing to prevent dry-firing or boiling the pipes.
* **Heater Cooldown:** If the heater is turned off (via thermostat or user command), the logic forces the filter pump to remain ON for a strict 5-minute cooldown period to strip residual heat from the heat exchanger.

### `PoolNetworkManager.h` (Network Stack)

#### 1. Standard C++ Memory Safety (`std::string`)
To prevent the heap fragmentation caused by the default Arduino `String` class over long uptimes, this class utilizes the C++ STL (`#include <string>`). Conversion to Arduino `String` or C-strings (`.c_str()`) is strictly quarantined to the immediate point of API execution, allowing variables to quickly go out of scope and free the heap.

#### 2. Self-Healing Connection & Captive Portal
* **Grace Period:** At boot, if the controller cannot find the router, it enters a 5-minute retry window to account for home routers booting up slowly after power outages.
* **Captive Portal:** If the 5-minute boot grace period expires without a connection, the controller assumes credentials have changed and spins up an Access Point (`192.168.4.1`) to serve a Wi-Fi setup page.
* **Background Auto-Recovery:** If the network drops *after* a successful boot, the controller silently drops to the `DISCONNECTED` state and attempts a non-blocking reconnection cycle every 20 seconds.

#### 3. W5500 SPI Ethernet (Core 3.x)
The KinCony board utilizes a W5500 hardware Ethernet chip. To maintain compatibility with Arduino ESP32 Core 3.x, the SPI bus is initialized globally with custom pins before being passed into the native `<ETH.h>` library. 

### `PoolMQTT.h` (Home Assistant Integration)

#### 1. Separation of Concerns
This class is exclusively responsible for translating `PoolLogic` state into Home Assistant entities. It does not handle device configuration (which is reserved for `PoolWebServer`). 

#### 2. Auto-Discovery & Dynamic Availability
The class utilizes Home Assistant's MQTT Auto-Discovery protocol. At boot, it publishes expansive JSON payloads to the `homeassistant/` root topic to automatically build the dashboard UI. It actively leverages MQTT availability topics (`online`/`offline`) to dynamically gray out the Pool Thermostat when Spa Mode is active, and vice versa.

#### 3. Buffer Expansion
Because the HA Auto-Discovery JSON payloads (especially for Climate entities) exceed the default limits of the `PubSubClient` library, the buffer is explicitly expanded to `1024` bytes at runtime during `begin()`.

### `PoolWebServer.h` (REST API & Configuration)

#### 1. Port Authority & Boundary Handoff
`PoolWebServer` owns Port 80. While the `WebServer` library forces the use of Arduino `String` objects to parse HTTP bodies, these objects are instantly converted to `std::string` at the boundary before being passed to `PoolLogic` or `PoolNetworkManager`.

#### 2. Atomicity & Safety Interlocks
The Web Server does not manipulate hardware. It passes requests to the `PoolLogic` authority. If a web request violates a safety rule, the Web Server catches the error returned by the logic layer and relays it back to the client as a `400 Bad Request` with a descriptive JSON payload for toast notifications.

### `WebUI.h` (User Interface)

#### 1. Zero-RAM Footprint (`PROGMEM`)
The entire Web Admin dashboard (HTML/CSS/JS) is stored as a raw string literal in the ESP32's **Flash memory** using the `PROGMEM` macro. This ensures that the 20KB+ UI does not occupy any space in the SRAM (Heap).

#### 2. Single Page Application (SPA)
The UI is built as a SPA. Tab switching and API calls are handled via asynchronous `fetch()`, allowing the background JavaScript to maintain a 2-second polling "heartbeat" with the controller for real-time sensor updates without page reloads.

## KinCony KC868-A8v3 Pin Reference Map

For developers cloning this repository, here is the factory hardware mapping for the ESP32-S3 PLC.

### I2C Bus & Expanders
* **SDA:** GPIO 8
* **SCL:** GPIO 18
* **PCF8575 (Relays/Digital Inputs):** Address `0x22`
  * `P0` - `P7`: Relays 1 through 8
  * `P8` - `P15`: Digital Inputs 1 through 8 (DI1 - DI8)
* **24C02 EEPROM:** Address `0x50`
* **DS3231 RTC:** Address `0x68`
* **SSD1306 OLED:** Address `0x3C`

### Analog & 1-Wire Sensors
* **Analog Input A1 (0-5V):** GPIO 7 *(Used for 10k NTC Water Temp)*
* **Analog Input A2 (0-5V):** GPIO 6
* **1-Wire S1 (w/ Hardware Pull-up):** GPIO 40 *(Used for DS18B20 Air Temp)*
* **1-Wire S2 (w/ Hardware Pull-up):** GPIO 13
* **1-Wire S3 (w/ Hardware Pull-up):** GPIO 48
* **1-Wire S4 (w/ Hardware Pull-up):** GPIO 14

### Ethernet (W5500 SPI)
* **CLK:** GPIO 1
* **MOSI:** GPIO 2
* **MISO:** GPIO 41
* **CS:** GPIO 42
* **INT:** GPIO 43
* **RST:** GPIO 44

### Miscellaneous I/O
* **SD Card (SPI):** MOSI(10), SCK(11), MISO(12), CS(9), CD(47)
* **RS485:** RXD(38), TXD(39)
* **RF433M:** Sender(4), Receiver(5)