#include "PoolLogic.h"

PoolLogic::PoolLogic(KinConyPLC& plc, NetworkManager& netMgr)
    : _plc(plc), _netMgr(netMgr), _currentMode(SystemMode::AUTO),
      _currentWaterMode(WaterMode::POOL), _targetTempPool(80.0), _targetTempSpa(100.00),
      _freezeProtectTemp(35.0), _fountainState(false), _vacuumState(false),
      _lightsState(false), _spaBlowerState(false), _heaterEnabled(false),
      _freezeModeActive(false), _callForHeat(false) {}

void PoolLogic::begin() {
    _prefs.begin("pool_settings", false);
    loadSettings();
    Serial.println("PoolLogic initialized. Defaulting to AUTO mode.");
}

void PoolLogic::loop() {
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
    _currentMode = mode;
    if (mode == SystemMode::AUTO) {
        cancelAllOverrides();
    }
}

SystemMode PoolLogic::getSystemMode() const {
    return _currentMode;
}

// Service mode only
void PoolLogic::setServiceRelay(PoolRelay relay, bool state) {
    if (_currentMode != SystemMode::SERVICE) {
        return;
    }
    // Service mode safety: do not allow heater enable or vacuum booster pump to run without filter pump running
    if ((relay == PoolRelay::HeaterIgniter || relay == PoolRelay::VacuumPump) && state == true) {
        if (!_plc.getRelayState(PoolRelay::FilterPump)) {
            Serial.println("Access denied: Cannot fire heater or enable vacuum without filter pump running.");
            return;
        }
    }
    _plc.setRelay(relay, state);
}

// User overrides
void PoolLogic::overrideWaterMode(WaterMode mode, uint16_t timeoutMinutes) {
    // HA cannot turn the pool off
    if (mode == WaterMode::OFF) {
        Serial.println("Access denied: OFF is only available in SERVICE mode.");
        return;
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
}

void PoolLogic::overrideFountain(bool state, uint16_t timeoutMinutes) {
    _fountainState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideFountainEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideFountainEnd = 0;
    }
}

void PoolLogic::overrideVacuum(bool state, uint16_t timeoutMinutes) {
    _vacuumState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideVacuumEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideVacuumEnd = 0;
    }
}

void PoolLogic::overrideLights(bool state, uint16_t timeoutMinutes) {
    _lightsState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideLightsEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideLightsEnd = 0;
    }
}

void PoolLogic::overrideSpaBlower(bool state, uint16_t timeoutMinutes) {
    _spaBlowerState = state;
    if (state) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideSpaBlowerEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideSpaBlowerEnd = 0;
    }
}

void PoolLogic::overrideHeater(bool enable, uint16_t timeoutMinutes) {
    if (_currentWaterMode == WaterMode::SPA) {
        return;
    }
    _heaterEnabled = enable;
    if (enable) {
        _currentMode = SystemMode::USER_OVERRIDE;
        _overrideHeaterEnd = millis() + (timeoutMinutes * 60000UL);
    }
    else {
        _overrideHeaterEnd = 0;
    }
}

// Schedules, thermostats, execution
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
}

void PoolLogic::executeHardwareStates() {
    // Actuators
    bool isSpa = (_currentWaterMode == WaterMode::SPA);
    _plc.setRelay(PoolRelay::IntakeActuator, isSpa);
    _plc.setRelay(PoolRelay::ReturnActuator, isSpa);
    // Filter pump
    DateTime now = _plc.getCurrentTime();
    bool isFilterScheduled = isTimeInSchedule(now, _schedFilter);
    bool pumpON = _freezeModeActive || isSpa || _vacuumState || (_currentWaterMode == WaterMode::POOL && isFilterScheduled);
    _plc.setRelay(PoolRelay::FilterPump, pumpON);
    // Independent accessories
    _plc.setRelay(PoolRelay::VacuumPump, _freezeModeActive || _vacuumState);
    _plc.setRelay(PoolRelay::PoolLights, _lightsState);
    _plc.setRelay(PoolRelay::SpaBlower, _spaBlowerState);
    _plc.setRelay(PoolRelay::AuxPump, _freezeModeActive || _fountainState);
    // Heater
    _plc.setRelay(PoolRelay::HeaterIgniter, _callForHeat);
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

float PoolLogic::getFreezeProtectTemp() const {
    return _freezeProtectTemp;
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