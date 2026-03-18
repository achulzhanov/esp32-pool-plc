#include <time.h>
#include <string>
#include "PoolLogic.h"

PoolLogic::PoolLogic(KinConyPLC& plc, PoolNetworkManager& netMgr)
    : _plc(plc), _netMgr(netMgr), _currentMode(SystemMode::AUTO),
      _currentWaterMode(WaterMode::POOL), _targetTempPool(80.0), _targetTempSpa(100.00),
      _freezeProtectTemp(35.0), _fountainState(false), _vacuumState(false),
      _lightsState(false), _spaBlowerState(false), _heaterEnabled(false),
      _freezeModeActive(false), _callForHeat(false), _lastCallForHeat(false), _heaterCooldownEnd(0) {}

void PoolLogic::begin() {
    // Set up the background NTP client for Central Time (US)
    // The POSIX string "CST6CDT,M3.2.0,M11.1.0" handles Daylight Saving Time automatically forever
    configTzTime("CST6CDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    _prefs.begin("pool_settings", false);
    loadSettings();
    Serial.println("PoolLogic initialized. Defaulting to AUTO mode.");
}

void PoolLogic::loop() {
    syncTime();
    evaluateSafetyInterlocks();
    // Service mode exits loop, web admin will call _plc.setRelay() directly
    if (_currentMode == SystemMode::SERVICE) {
        return;
    }
    evaluateOverrideTimeouts();
    evaluateSchedules();
    evaluateThermostat();
    executeHardwareStates();
}

void PoolLogic::evaluateSafetyInterlocks() {
    // Network fallback: If in USER_OVERRIDE but network drops,
    // we cannot safely allow it to run forever since HA can't turn it off.
    if (_currentMode == SystemMode::USER_OVERRIDE && !_netMgr.isConnected()) {
        Serial.println("Warning: Network lost during User Override. Reverting to AUTO.");
        cancelAllOverrides();
    }
    // Freeze protection
    float currentAirTemp = _plc.getAirTemp();
    if (currentAirTemp > -100.0 && currentAirTemp <= _freezeProtectTemp) {
        if (!_freezeModeActive) {
            Serial.println("Warning: Freezing temperatures detected. Enabling freeze protection.");
            _freezeModeActive = true;
        }
    }
    else {
        if (_freezeModeActive) {
            Serial.println("Temperature above freezing. Disabling freeze protection.");
            _freezeModeActive = false;
        }
    }
}

void PoolLogic::evaluateOverrideTimeouts() {
    unsigned long currentMillis = millis();
    bool anyOverrideActive = false;
    // Check heater cooldown timeout
    if (_heaterCooldownEnd > 0 && (currentMillis >= _heaterCooldownEnd)) {
        Serial.println("Heater cooldown period finished. Pump may now turn off.");
        _heaterCooldownEnd = 0;
    }
    // Check Water Mode timeout
    if (_currentWaterMode != WaterMode::POOL && (currentMillis >= _overrideWaterModeEnd)) {
        Serial.println("Water Mode override expired. Reverting to POOL.");
        _currentWaterMode = WaterMode::POOL;
        _overrideWaterModeEnd = 0;
        _spaBlowerState = false;
        _overrideSpaBlowerEnd = 0;
        _heaterEnabled = false;
        _overrideHeaterEnd = 0;
    }
    else if (_overrideWaterModeEnd > 0) {
        anyOverrideActive = true;
    }
    // Check independent accessory timeouts
    if (_overrideFountainEnd > 0 && (currentMillis >= _overrideFountainEnd)) {
        _fountainState = false; _overrideFountainEnd = 0;
    }
    else if (_overrideFountainEnd > 0) {
        anyOverrideActive = true;
    }
    if (_overrideLightsEnd > 0 && (currentMillis >= _overrideLightsEnd)) {
        _lightsState = false; _overrideLightsEnd = 0;
    }
    else if (_overrideLightsEnd > 0) {
        anyOverrideActive = true;
    }
    if (_overrideVacuumEnd > 0 && (currentMillis >= _overrideVacuumEnd)) {
        _vacuumState = false; _overrideVacuumEnd = 0;
    }
    else if (_overrideVacuumEnd > 0) {
        anyOverrideActive = true;
    }
    if (_overrideSpaBlowerEnd > 0 && (currentMillis >= _overrideSpaBlowerEnd)) {
        _spaBlowerState = false; _overrideSpaBlowerEnd = 0;
    }
    else if (_overrideSpaBlowerEnd > 0) {
        anyOverrideActive = true;
    }
    if (_overrideHeaterEnd > 0 && (currentMillis >= _overrideHeaterEnd)) {
        Serial.println("Heater override expired. Disabling heater.");
        _heaterEnabled = false;
        _overrideHeaterEnd = 0;
    }
    else if (_overrideHeaterEnd > 0) {
        anyOverrideActive = true;
    }
    if (!anyOverrideActive && _currentMode == SystemMode::USER_OVERRIDE) {
        Serial.println("All User Overrides expired. Returning to AUTO mode.");
        _currentMode = SystemMode::AUTO;
    }
}

void PoolLogic::cancelAllOverrides() {
    _currentMode = SystemMode::AUTO;
    _currentWaterMode = WaterMode::POOL;
    _fountainState = false;
    _vacuumState = false;
    _lightsState = false;
    _spaBlowerState = false;
    _heaterEnabled = false;
    _overrideWaterModeEnd = 0;
    _overrideFountainEnd = 0;
    _overrideVacuumEnd = 0;
    _overrideLightsEnd = 0;
    _overrideSpaBlowerEnd = 0;
    _overrideHeaterEnd = 0;
    Serial.println("Overrides cancelled. Reverted to AUTO / POOL mode.");
}

void PoolLogic::setSystemMode(SystemMode mode) {
    if (_currentMode == SystemMode::SERVICE && mode != SystemMode::SERVICE) {
        // If the physical heater relay is currently ON when we exit manual control
        if (_plc.getRelayState(PoolRelay::HeaterIgniter)) {
            Serial.println("Exiting Service Mode with heater ON. Forcing 5-min pump cooldown.");
            _heaterCooldownEnd = millis() + (5 * 60000UL);
            _lastCallForHeat = false; // Reset to prevent double-triggering in loop
        }
    }
    _currentMode = mode;
    if (mode == SystemMode::AUTO) {
        cancelAllOverrides();
    }
}

SystemMode PoolLogic::getSystemMode() const {
    return _currentMode;
}

// Service mode only
std::string PoolLogic::setServiceRelay(PoolRelay relay, bool state) {
    if (_currentMode != SystemMode::SERVICE) {
        return "System must be in SERVICE mode to manually toggle relays.";
    }
    // Safety 1: Cannot turn heater or vacuum ON without filter pump
    if ((relay == PoolRelay::HeaterIgniter || relay == PoolRelay::VacuumPump) && state == true) {
        if (!_plc.getRelayState(PoolRelay::FilterPump)) {
            return "Safety Interlock: Filter pump must be ON to enable heater or vacuum.";
        }
    }
    // Safety 2: Track Heater turning OFF manually to start the cooldown timer
    if (relay == PoolRelay::HeaterIgniter && state == false) {
        if (_plc.getRelayState(PoolRelay::HeaterIgniter) == true) {
            // It was physically ON, start the 5-minute timer
            _heaterCooldownEnd = millis() + (5 * 60000UL); 
        }
    }
    // Safety 3: Trying to turn OFF the filter pump
    if (relay == PoolRelay::FilterPump && state == false) {
        // Case A: Heater is currently ON
        if (_plc.getRelayState(PoolRelay::HeaterIgniter) == true) {
            _plc.setRelay(PoolRelay::HeaterIgniter, false);
            _plc.setRelay(PoolRelay::VacuumPump, false);
            _heaterCooldownEnd = millis() + (5 * 60000UL);
            return "Safety Interlock: Heater was running. Heater disabled, but pump locked ON for 5 min cooldown.";
        }
        // Case B: Heater is OFF, but cooldown timer is still actively ticking
        if (_heaterCooldownEnd > 0 && millis() < _heaterCooldownEnd) {
            unsigned long remainingSeconds = (_heaterCooldownEnd - millis()) / 1000;
            return "Safety Interlock: Cooldown active. Pump locked ON for " + std::to_string(remainingSeconds) + " more seconds.";
        }
        // Case C: Cooldown is finished (or was never active). Safe to turn off pump.
        _plc.setRelay(PoolRelay::VacuumPump, false);
        _heaterCooldownEnd = 0;
    }
    _plc.setRelay(relay, state);
    return "success";
}

bool PoolLogic::getRelayState(PoolRelay relay) const {
    return _plc.getRelayState(relay);
}

// User overrides
std::string PoolLogic::overrideWaterMode(WaterMode mode, uint16_t timeoutMinutes) {
    // HA cannot turn the pool off
    if (mode == WaterMode::OFF) {
        Serial.println("Access denied: OFF is only available in SERVICE mode.");
        return "Access denied: OFF is only available in SERVICE mode.";
    }
    _currentWaterMode = mode;
    if (mode == WaterMode::SPA) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideWaterModeEnd = millis() + (timeoutMinutes * 60000UL);
        // Auto enable spa features and tie to spa timeout
        _spaBlowerState = true;
        _overrideSpaBlowerEnd = _overrideWaterModeEnd;
        _heaterEnabled = true;
        _overrideHeaterEnd = _overrideWaterModeEnd;
        _lightsState = true;
        _overrideLightsEnd = _overrideWaterModeEnd;
    }
    else {
        // User reverted to POOL
        _overrideWaterModeEnd = 0;
        _spaBlowerState = false;
        _overrideSpaBlowerEnd = 0;
        _heaterEnabled = false;
        _overrideHeaterEnd = 0;
        _lightsState = false;
        _overrideLightsEnd = 0;
    }
    return "success";
}

std::string PoolLogic::overrideFountain(bool state, uint16_t timeoutMinutes) {
    _fountainState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideFountainEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideFountainEnd = 0;
    }
    return "success";
}

std::string PoolLogic::overrideVacuum(bool state, uint16_t timeoutMinutes) {
    _vacuumState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideVacuumEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideVacuumEnd = 0;
    }
    return "success";
}

std::string PoolLogic::overrideLights(bool state, uint16_t timeoutMinutes) {
    _lightsState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideLightsEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideLightsEnd = 0;
    }
    return "success";
}

std::string PoolLogic::overrideSpaBlower(bool state, uint16_t timeoutMinutes) {
    _spaBlowerState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideSpaBlowerEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideSpaBlowerEnd = 0;
    }
    return "success";
}

std::string PoolLogic::overrideHeater(bool enable, uint16_t timeoutMinutes) {
    if (_currentWaterMode == WaterMode::SPA) {
        return "Heater is automatically controlled by Spa Mode.";
    }
    _heaterEnabled = enable;
    if (enable) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideHeaterEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideHeaterEnd = 0;
    }
    return "success";
}

// Schedules, thermostats, execution
void PoolLogic::syncTime() {
    // Only attempt to sync if we have a network connection
    if (WiFi.status() != WL_CONNECTED) return;
    DateTime rtcTime = _plc.getCurrentTime();
    unsigned long currentMillis = millis();
    // Sync if the RTC is lost in the past (e.g. year 2000) OR once every 24 hours
    if (rtcTime.year() < 2024 || (currentMillis - _lastTimeSync > 86400000)) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 5000)) { 
            // Convert the ESP32's internal time struct to RTClib's DateTime format
            DateTime ntpTime(
                timeinfo.tm_year + 1900, 
                timeinfo.tm_mon + 1, 
                timeinfo.tm_mday, 
                timeinfo.tm_hour, 
                timeinfo.tm_min, 
                timeinfo.tm_sec
            );
            // Burn it into the KinCony's DS3231 chip
            _plc.setTime(ntpTime);
            _lastTimeSync = currentMillis;
            Serial.print("RTC successfully synced with NTP. Current time is: ");
            Serial.println(getCurrentTimeString().c_str());
        }
    }
}

bool PoolLogic::isTimeInSchedule(const DateTime& now, const Schedule& sched) const {
    if (!sched.enabled) {
        return false;
    }
    int currentMins = now.hour() * 60 + now.minute();
    int startMins = sched.startHour * 60 + sched.startMin;
    int endMins = sched.endHour * 60 + sched.endMin;
    if (endMins >= startMins) {
        return (currentMins >= startMins && currentMins < endMins);
    }
    else {
        return (currentMins >= startMins || currentMins < endMins);
    }
}

void PoolLogic::evaluateSchedules() {
    DateTime now = _plc.getCurrentTime();
    if (_overrideVacuumEnd == 0) {
        _vacuumState = isTimeInSchedule(now, _schedVacuum);
    }
    if (_overrideLightsEnd == 0) {
        _lightsState = isTimeInSchedule(now, _schedLights);
    }
    // Filter pump schedule evaluated in executeHardwareStates()
    // because it is a master dependency
}

void PoolLogic::evaluateThermostat() {
    float currentTemp = _plc.getWaterTemp();
    // Target water temp dependent on intake actuator position
    float targetTemp = (_currentWaterMode == WaterMode::SPA) ? _targetTempSpa : _targetTempPool;
    bool heatingAllowed = (_currentWaterMode == WaterMode::SPA) || _heaterEnabled;
    // Sensor failsafe
    if (currentTemp < 32.0 || currentTemp > 110.0) {
        heatingAllowed = false;
    }
    // Hysteresis
    if (heatingAllowed && currentTemp < (targetTemp - 1.0)) {
        _callForHeat = true;
    }
    else if (!heatingAllowed || currentTemp >= targetTemp) {
        _callForHeat = false;
    }
    // Filter pump interlock
    DateTime now = _plc.getCurrentTime();
    bool isFilterScheduled = isTimeInSchedule(now, _schedFilter);
    bool pumpWillRun = _freezeModeActive || _vacuumState ||
        (_currentWaterMode == WaterMode::SPA) ||
        (_currentWaterMode == WaterMode::POOL && isFilterScheduled);
    if (!pumpWillRun) {
        _callForHeat = false;
    }
    // If the heater was ON last loop, but is OFF this loop, start the timer
    if (_lastCallForHeat == true && _callForHeat == false) {
        Serial.println("Heater turned off. Initiating 5-minute pump cooldown.");
        _heaterCooldownEnd = millis() + (5 * 60000UL); // 5 minutes
    }
    // Save the current state for the next loop to compare against
    _lastCallForHeat = _callForHeat;
}

void PoolLogic::executeHardwareStates() {
    // Actuators
    bool isSpa = (_currentWaterMode == WaterMode::SPA);
    _plc.setRelay(PoolRelay::IntakeActuator, isSpa);
    _plc.setRelay(PoolRelay::ReturnActuator, isSpa);
    // Filter pump
    DateTime now = _plc.getCurrentTime();
    bool isFilterScheduled = isTimeInSchedule(now, _schedFilter);
    bool isCooldownActive = (_heaterCooldownEnd > 0);
    bool pumpON = _freezeModeActive || isSpa || _vacuumState ||
                  (_currentWaterMode == WaterMode::POOL && isFilterScheduled) || isCooldownActive;
    _plc.setRelay(PoolRelay::FilterPump, pumpON);
    // Independent accessories
    _plc.setRelay(PoolRelay::VacuumPump, _freezeModeActive || _vacuumState);
    _plc.setRelay(PoolRelay::PoolLights, _lightsState);
    _plc.setRelay(PoolRelay::SpaBlower, _spaBlowerState);
    _plc.setRelay(PoolRelay::AuxPump, _freezeModeActive || _fountainState);
    // Heater
    _plc.setRelay(PoolRelay::HeaterIgniter, _callForHeat);
}

std::string PoolLogic::getCurrentTimeString() const {
    DateTime now = _plc.getCurrentTime();
    char timeBuffer[] = "YYYY-MM-DD hh:mm:ss";
    // RTClib mutates the array in place
    now.toString(timeBuffer);
    // Return a standard C++ string
    return std::string(timeBuffer);
}

void PoolLogic::setTargetTempPool(float temp) {
    _targetTempPool = temp;
    saveSettings();
}

void PoolLogic::setTargetTempSpa(float temp) {
    _targetTempSpa = temp;
    saveSettings();
}

void PoolLogic::setFreezeProtectTemp(float temp) {
    _freezeProtectTemp = temp;
    saveSettings();
}

void PoolLogic::setWaterTempOffset(float offset) {
    _plc.setWaterTempOffset(offset);
    saveSettings();
}

void PoolLogic::setScheduleFilter(const Schedule& sched) {
    _schedFilter = sched;
    saveSettings();
}

void PoolLogic::setScheduleVacuum(const Schedule& sched) {
    _schedVacuum = sched;
    saveSettings(); 
}

void PoolLogic::setScheduleLights(const Schedule& sched) {
    _schedLights = sched;
    saveSettings(); 
}

float PoolLogic::getTargetTempPool() const {
    return _targetTempPool;
}

float PoolLogic::getTargetTempSpa() const {
    return _targetTempSpa;
}

float PoolLogic::getAirTemp() const {
    return _plc.getAirTemp();
}

float PoolLogic::getWaterTemp() const {
    return _plc.getWaterTemp();
}

float PoolLogic::getFreezeProtectTemp() const {
    return _freezeProtectTemp;
}

float PoolLogic::getWaterTempOffset() const {
    return _plc.getWaterTempOffset();
}

Schedule PoolLogic::getScheduleFilter() const {
    return _schedFilter;
}

Schedule PoolLogic::getScheduleVacuum() const {
    return _schedVacuum;
}

Schedule PoolLogic::getScheduleLights() const {
    return _schedLights;
}

WaterMode PoolLogic::getWaterMode() const {
    return _currentWaterMode;
}

bool PoolLogic::isFountainOn() const {
    return _fountainState;
}

bool PoolLogic::isVacuumOn() const {
    return _vacuumState;
}

bool PoolLogic::isLightsOn() const {
    return _lightsState;
}

bool PoolLogic::isSpaBlowerOn() const {
    return _spaBlowerState;
}

bool PoolLogic::isFreezeProtectionActive() const {
    return _freezeModeActive;
}

bool PoolLogic::isHeaterEnabled() const {
    return _heaterEnabled;
}

bool PoolLogic::isHeaterActive() const { 
    return _plc.getRelayState(PoolRelay::HeaterIgniter); 
}

void PoolLogic::loadSettings() {
    // Temperatures (Key, Default Value)
    _targetTempPool = _prefs.getFloat("t_pool", 80.0);
    _targetTempSpa = _prefs.getFloat("t_spa", 100.0);
    _freezeProtectTemp = _prefs.getFloat("t_frz", 35.0);
    // Filter Schedule (Default: 8:00 AM to 8:00 PM, Enabled)
    _schedFilter.enabled = _prefs.getBool("sf_en", true);
    _schedFilter.startHour = _prefs.getUChar("sf_sh", 8);
    _schedFilter.startMin = _prefs.getUChar("sf_sm", 0);
    _schedFilter.endHour = _prefs.getUChar("sf_eh", 20);
    _schedFilter.endMin = _prefs.getUChar("sf_em", 0);
    // Vacuum Schedule (Default: 8:15 AM to 12:00 PM, Enabled)
    // Staggered start up for inrush current and priming
    _schedVacuum.enabled = _prefs.getBool("sv_en", true);
    _schedVacuum.startHour = _prefs.getUChar("sv_sh", 8);
    _schedVacuum.startMin = _prefs.getUChar("sv_sm", 15);
    _schedVacuum.endHour = _prefs.getUChar("sv_eh", 12);
    _schedVacuum.endMin = _prefs.getUChar("sv_em", 0);
    // Lights Schedule (Default: 7:00 PM to 10:00 PM, Enabled)
    _schedLights.enabled = _prefs.getBool("sl_en", true);
    _schedLights.startHour = _prefs.getUChar("sl_sh", 19);
    _schedLights.startMin = _prefs.getUChar("sl_sm", 0);
    _schedLights.endHour = _prefs.getUChar("sl_eh", 22);
    _schedLights.endMin = _prefs.getUChar("sl_em", 0);
    Serial.println("Settings successfully loaded from NVS flash memory.");
}

void PoolLogic::saveSettings() {    
    // Temperatures
    _prefs.putFloat("t_pool", _targetTempPool);
    _prefs.putFloat("t_spa", _targetTempSpa);
    _prefs.putFloat("t_frz", _freezeProtectTemp);
    // Filter Schedule
    _prefs.putBool("sf_en", _schedFilter.enabled);
    _prefs.putUChar("sf_sh", _schedFilter.startHour);
    _prefs.putUChar("sf_sm", _schedFilter.startMin);
    _prefs.putUChar("sf_eh", _schedFilter.endHour);
    _prefs.putUChar("sf_em", _schedFilter.endMin);
    // Vacuum Schedule
    _prefs.putBool("sv_en", _schedVacuum.enabled);
    _prefs.putUChar("sv_sh", _schedVacuum.startHour);
    _prefs.putUChar("sv_sm", _schedVacuum.startMin);
    _prefs.putUChar("sv_eh", _schedVacuum.endHour);
    _prefs.putUChar("sv_em", _schedVacuum.endMin);
    // Lights Schedule
    _prefs.putBool("sl_en", _schedLights.enabled);
    _prefs.putUChar("sl_sh", _schedLights.startHour);
    _prefs.putUChar("sl_sm", _schedLights.startMin);
    _prefs.putUChar("sl_eh", _schedLights.endHour);
    _prefs.putUChar("sl_em", _schedLights.endMin);
    Serial.println("Settings saved to NVS flash memory.");
}