#include "KinConyPLC.h"

// All relays off at boot
KinConyPLC::KinConyPLC() : _currentRelayState(0x0000), _oneWire(PIN_1WIRE_AIR), _airSensor(&_oneWire) {}
KinConyPLC::~KinConyPLC() {}

void KinConyPLC::begin() {
    // Initialize I2C bus
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    // Initialize sensors
    _airSensor.begin();
    _airSensor.setWaitForConversion(false);
    // Initialize RTC
    _rtc.begin();
    // Open "pool" storage folder and fetch saved temp offset
    _prefs.begin("pool", false);
    _waterTempOffset = _prefs.getFloat("tempOffset", 0.0);
    // All relays off at boot
    writeI2C(_currentRelayState);
}

void KinConyPLC::setRelay(PoolRelay relay, bool state) {
    // Convert enum to standard int to get bit position 0-7
    uint8_t bitPosition = static_cast<uint8_t>(relay);
    // Create mask with single 1 (target relay)
    uint16_t mask = (1 << bitPosition);
    if (state == true) {
        // Turn on (bitwise OR)
        _currentRelayState = _currentRelayState | mask;
    }
    else {
        // Turn off (bitwise AND with inverted mask)
        uint16_t invertedMask = ~mask;
        _currentRelayState = _currentRelayState & invertedMask;
    }
    writeI2C(_currentRelayState);
}

bool KinConyPLC::getRelayState(PoolRelay relay) const {
    uint8_t bitPosition = static_cast<uint8_t>(relay);
    uint16_t mask = (1 << bitPosition);
    uint16_t isolatedBit = _currentRelayState & mask;
    if (isolatedBit > 0) {
        return true;
    }
    else {
        return false;
    }
}

void KinConyPLC::writeI2C(uint16_t data) {
    // PCF8575 is active-low, so we need to invert the bits
    uint16_t invertedData = ~data;
    Wire.beginTransmission(I2C_ADDR_PCF8575);
    Wire.write(lowByte(invertedData));
    Wire.write(highByte(invertedData));
    Wire.endTransmission();
}

// Analog read and Steinhart-Hart conversion
float KinConyPLC::getWaterTemp() const {
    int analogValue = analogRead(PIN_ANALOG_WATER);
    if (analogValue == 0) {
        return -127.0;
    }
    // Convert 12-bit ADC to resistance
    // Assumes 10kOhm series resistor
    float resistance = (4095.0 / analogValue) - 1.0;
    resistance = 10000.0 / resistance;
    // Steinhart-Hart equation (NTC 10k 3950)
    float steinhart = resistance / 10000.0;
    steinhart = log(steinhart);
    steinhart /= 3950.0;
    steinhart += 1.0 / (25.0 + 273.15);
    steinhart = 1.0 / steinhart;
    steinhart -= 273.15;
    // Convert to Fahrenheit
    float tempF = (steinhart * 9.0 / 5.0) + 32.0;
    return tempF + _waterTempOffset;
}

void KinConyPLC::setWaterTempOffset(float offset) {
    _waterTempOffset = offset;
    _prefs.putFloat("tempOffset", offset);
}

float KinConyPLC::getWaterTempOffset() const {
    return _waterTempOffset;
}

float KinConyPLC::getAirTemp() {
    float tempF = _airSensor.getTempFByIndex(0);
    _airSensor.requestTemperatures();
    return tempF;
}

DateTime KinConyPLC::getCurrentTime() {
    return _rtc.now();
}

void KinConyPLC::setTime(const DateTime& dt) {
    _rtc.adjust(dt);
}