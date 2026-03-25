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

bool KinConyPLC::isServiceButtonActive() {
    // Read all 16 bits from PCF8575. The chip sends low byte (P0-P7, relays) first,
    // then high byte (P8-P15, digital inputs). DI1 = P8 = bit 0 of the high byte.
    // The NO button connects DI1 to GND, so active = LOW = 0.
    Wire.requestFrom((uint8_t)I2C_ADDR_PCF8575, (uint8_t)2);
    if (Wire.available() < 2) return false; // Safe default: not active on I2C failure
    Wire.read();                             // Discard low byte (relay states)
    uint8_t hi = Wire.read();               // High byte holds DI1-DI8
    return !(hi & 0x01);                    // DI1 = bit 0; active LOW, so invert
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
    // The KinCony A1 terminal has an internal hardware voltage divider between the
    // terminal block and the ESP32 ADC pin (input protection for industrial 0-5V signals).
    // The ADC does NOT read the terminal voltage directly.
    //
    // Measurements (desk, NTC disconnected, external 10kΩ pull-up to 3.304V):
    //   Terminal voltage: 1.990V   →  R_int = 10k × 1.990 / (3.304 - 1.990) = 15,145Ω
    //   ADC count:        ~1540    →  Confirmed by ghost reading of ~78°F = 25°C = 10kΩ NTC
    //
    // These two constants fully capture the hardware:
    //   R_THEV  = Thevenin resistance seen from terminal = R_ext ∥ R_int = 10k ∥ 15.1k
    //   ADC_OC  = ADC count when NTC is disconnected (open circuit)
    const float R_THEV = 6022.0f; // Ω — derived from terminal voltage measurement
    const int   ADC_OC = 1520;    // counts — calibrate with NTC disconnected
    if (analogValue <= 10 || analogValue >= ADC_OC - 50) {
        return -127.0f; // Short circuit (≤10) or open circuit / sensor fault (≥ADC_OC)
    }
    // NTC = R_THEV × ADC / (ADC_OC − ADC)
    // Derivation: Thevenin divider, V_A = V_oc × NTC/(R_THEV + NTC), ADC ∝ V_A
    float resistance = R_THEV * analogValue / (ADC_OC - analogValue);
    // Steinhart-Hart equation (NTC 10k B=3950)
    float steinhart = resistance / 10000.0f;
    steinhart = log(steinhart);
    steinhart /= 3950.0f;
    steinhart += 1.0f / (25.0f + 273.15f);
    steinhart = 1.0f / steinhart;
    steinhart -= 273.15f;
    // Convert Celsius to Fahrenheit
    return (steinhart * 9.0f / 5.0f) + 32.0f + _waterTempOffset;
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