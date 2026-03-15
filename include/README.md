# Header & Hardware Architecture (`include/`)

This directory contains the C++ header files for the ESP32-S3 Smart Pool Controller. The project uses OOP to separate concerns into dedicated classes that abstract the hardware layer, network stack, and control logic.

## Architectural Decisions

If you are modifying this code to fit your specific pool setup, please note the following design constraints driven by the KinCony KC868-A8v3 hardware and the ESP32-S3 RTOS environment:

### `KinConyPLC.h` (Hardware Class)

#### 1. 16-Bit Relay State (`uint16_t _currentRelayState`)
While this board only has 8 physical relays, the variable tracking their state is explicitly defined as a 16-bit integer (`uint16_t`). 
The relays are driven by a **PCF8575 I2C I/O Expander**. This specific microchip is a 16-bit device. Pins `P0-P7` drive the relays, while pins `P8-P15` are wired to the digital inputs. 
To prevent accidentally overwriting or disabling the digital inputs when toggling a relay, the software must read, mask, and transmit a full 16-bit payload during every I2C transaction. Do not reduce this variable to an 8-bit integer.

#### 2. Strict Type Safety (`enum class PoolRelay : uint8_t`)
To prevent magic numbers and out-of-bounds relay calls, the relays are strictly mapped using an `enum class`.
If you change the physical wiring of your high-voltage contactors or 24VAC actuators, you **must** update the names in the `PoolRelay` enum to match your new layout. 
The enum is explicitly packed into a `uint8_t` (1 byte) to conserve RAM, as we only need to map indexes 0 through 7.

---

### `NetworkManager.h` (Network Stack)

#### 1. Standard C++ Memory Safety (`std::string`)
To prevent the heap fragmentation caused by the default Arduino `String` class over long uptimes, this class utilizes the C++ STL (`#include <string>`). Conversion to Arduino `String` or C-strings (`.c_str()`) is strictly quarantined to the immediate point of hardware or Preferences API execution, allowing the variables to immediately go out of scope and free the heap.

#### 2. Non-Blocking Event Handlers (FreeRTOS Safety)
The `networkEventCallback` function is triggered by the ESP32's underlying FreeRTOS Wi-Fi thread. To prevent watchdog timer crashes, this static callback **never** attempts to reconnect or read from flash memory directly. It strictly acts as an interrupt, flipping the `_currentState` flag in roughly one microsecond and yielding control. The actual reconnection logic is safely handled by the `loop()` function running on the main program thread.

#### 3. Self-Healing Network Connection & Boot Grace Period
The network logic is designed to survive unstable grids and power flickers without stranding the pool controller offline:
* **Grace Period:** At boot, if the controller cannot find the router, it will enter a 5-minute retry window. This accounts for scenarios where the ESP32 boots in 2 seconds, but a standard home router takes several minutes to establish a Wi-Fi network after a power outage.
* **Captive Portal:** If (and only if) the 5-minute boot grace period expires without a connection, the controller assumes the network credentials have changed or the network is otherwise unavailable and spins up the SoftAP Captive Portal.
* **Background Auto-Recovery:** If the network drops *after* a successful boot, the controller does not enter AP Mode. It silently drops to the `DISCONNECTED` state and attempts a non-blocking reconnection cycle every 10 seconds until the router returns, ensuring the physical pool logic is never interrupted.

#### 4. W5500 SPI Ethernet (Core 3.x)
The KinCony board utilizes a W5500 hardware Ethernet chip. To maintain compatibility with Arduino ESP32 Core 3.x, the SPI bus is initialized globally with custom pins (`SPI.begin()`) before being passed into the native `ETH.begin()` function. Do not attempt to use the legacy `arduino-libraries/Ethernet` package, as the native `<ETH.h>` library provides vastly superior RTOS integration.

---

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