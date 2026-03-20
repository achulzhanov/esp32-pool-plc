#ifndef KINCONY_PLC_H
#define KINCONY_PLC_H

#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>
#include <Preferences.h>

// Index 0-7 maps to PCF8575 pins P0-P7
enum class PoolRelay : uint8_t {
    IntakeActuator = 0,  // Relay 1 (24VAC) — AC wiring kept together on relays 1-2
    ReturnActuator = 1,  // Relay 2 (24VAC)
    FilterPump = 2,      // Relay 3 (24VDC)
    AuxPump = 3,         // Relay 4 (24VDC)
    VacuumPump = 4,      // Relay 5 (24VDC)
    SpaBlower = 5,       // Relay 6 (24VDC)
    PoolLights = 6,      // Relay 7 (24VDC)
    HeaterIgniter = 7    // Relay 8 (Fireman switch)
};

class KinConyPLC {
public:
    KinConyPLC();
    ~KinConyPLC();

    static constexpr uint8_t PIN_I2C_SDA = 8;
    static constexpr uint8_t PIN_I2C_SCL = 18;
    static constexpr uint8_t I2C_ADDR_PCF8575 = 0x22;
    static constexpr uint8_t I2C_ADDR_DS3231 = 0x68;

    static constexpr uint8_t PIN_ANALOG_WATER = 7; // A1
    static constexpr uint8_t PIN_1WIRE_AIR = 40;   // S1

    void begin();
    void setRelay(PoolRelay relay, bool state);
    bool getRelayState(PoolRelay relay) const;
    float getWaterTemp() const;
    void setWaterTempOffset(float offset);
    float getWaterTempOffset() const;
    float getAirTemp();
    DateTime getCurrentTime();
    void setTime(const DateTime& dt);

private:
    uint16_t _currentRelayState; // 16-bit PCF8575 I2C expander
    OneWire _oneWire;
    DallasTemperature _airSensor;
    RTC_DS3231 _rtc;
    Preferences _prefs;
    float _waterTempOffset = 0.0;
    void writeI2C(uint16_t data);
};

#endif