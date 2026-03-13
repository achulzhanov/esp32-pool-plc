# Header & Hardware Architecture (`include/`)

This directory contains the C++ header files for the ESP32-S3 Smart Pool Controller. The primary driver, `KinConyPLC.h`, acts as the blueprint for the hardware abstraction layer.

## Architectural Decisions

If you are modifying this code to fit your specific pool setup, please note the following design constraints driven by the KinCony KC868-A8v3 hardware:

### 1. 16-Bit Relay State (`uint16_t _currentRelayState`)
While this board only has 8 physical relays, the variable tracking their state is explicitly defined as a 16-bit integer (`uint16_t`). 
The relays are driven by a **PCF8575 I2C I/O Expander**. This specific microchip is a 16-bit device. Pins `P0-P7` drive the relays, while pins `P8-P15` are wired to the digital inputs. 
To prevent accidentally overwriting or disabling the digital inputs when toggling a relay, the software must read, mask, and transmit a full 16-bit payload during every I2C transaction. Do not reduce this variable to an 8-bit integer.

### 2. Strict Type Safety (`enum class PoolRelay : uint8_t`)
To prevent magic numbers and out-of-bounds relay calls, the relays are strictly mapped using an `enum class`.
If you change the physical wiring of your high-voltage contactors or 24VAC actuators, you **must** update the names in the `PoolRelay` enum to match your new layout. 
The enum is explicitly packed into a `uint8_t` (1 byte) to conserve RAM, as we only need to map indexes 0 through 7.

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