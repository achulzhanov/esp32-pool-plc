#ifndef POOL_LOGIC_H
#define POOL_LOGIC_H

#include <Arduino.h>
#include <Preferences.h>
#include <string>
#include "KinConyPLC.h"
#include "PoolNetworkManager.h"

enum class SystemMode : uint8_t {
    AUTO,
    USER_OVERRIDE,
    SERVICE
};

enum class WaterMode : uint8_t {
    OFF,
    POOL,
    SPA
};

struct Schedule {
    uint8_t startHour;
    uint8_t startMin;
    uint8_t endHour;
    uint8_t endMin;
    bool enabled;
};

class PoolLogic {
public:
    // Constructor takes reference to existing classes
    PoolLogic(KinConyPLC& plc, PoolNetworkManager& netMgr);

    void begin();
    void loop();

    // Service mode (Web admin)
    void setSystemMode(SystemMode mode);
    SystemMode getSystemMode() const;
    std::string setServiceRelay(PoolRelay relay, bool state);
    bool getRelayState(PoolRelay relay) const;

    // User overrides
    std::string overrideWaterMode(WaterMode mode, uint16_t timeoutMinutes);
    std::string overrideFountain(bool state, uint16_t timeoutMinutes);
    std::string overrideVacuum(bool state, uint16_t timeoutMinutes);
    std::string overrideLights(bool state, uint16_t timeoutMinutes);
    std::string overrideSpaBlower(bool state, uint16_t timeoutMinutes);
    std::string overrideHeater(bool enable, uint16_t timeoutMinutes);

    void cancelAllOverrides();

    // Schedules & settings
    void setTargetTempPool(float temp);
    void setTargetTempSpa(float temp);
    void setFreezeProtectTemp(float temp);
    void setWaterTempOffset(float offset);
    float getTargetTempPool() const;
    float getTargetTempSpa() const;
    float getAirTemp() const;
    float getWaterTemp() const;
    float getFreezeProtectTemp() const;
    float getWaterTempOffset() const;
    void setScheduleFilter(const Schedule& sched);
    void setScheduleVacuum(const Schedule& sched);
    void setScheduleLights(const Schedule& sched);
    Schedule getScheduleFilter() const;
    Schedule getScheduleVacuum() const;
    Schedule getScheduleLights() const;

    //Read states
    WaterMode getWaterMode() const;
    bool isFountainOn() const;
    bool isVacuumOn() const;
    bool isLightsOn() const;
    bool isSpaBlowerOn() const;
    bool isHeaterEnabled() const;
    bool isHeaterActive() const;
    bool isFreezeProtectionActive() const;
    std::string getCurrentTimeString() const;
    int getSpaTimeRemaining() const;
    int getHeaterTimeRemaining() const;
    int getLightsTimeRemaining() const;
    int getVacuumTimeRemaining() const;
    int getFountainTimeRemaining() const;

private:
    KinConyPLC& _plc;
    PoolNetworkManager& _netMgr;
    Preferences _prefs;

    SystemMode _currentMode;
    WaterMode _currentWaterMode;

    float _targetTempPool;
    float _targetTempSpa;
    float _freezeProtectTemp;

    bool _fountainState;
    bool _vacuumState;
    bool _lightsState;
    bool _spaBlowerState;
    bool _heaterEnabled;
    bool _freezeModeActive;
    bool _callForHeat;
    bool _lastCallForHeat;

    Schedule _schedFilter;
    Schedule _schedVacuum;
    Schedule _schedLights;
    unsigned long _overrideWaterModeEnd;
    unsigned long _overrideFountainEnd;
    unsigned long _overrideVacuumEnd;
    unsigned long _overrideSpaBlowerEnd;
    unsigned long _overrideLightsEnd;
    unsigned long _overrideHeaterEnd;
    unsigned long _heaterCooldownEnd;

    unsigned long _lastTimeSync = 0;
    unsigned long _lastFailsafePrint = 0;
    void syncTime();

    bool isTimeInSchedule(const DateTime& now, const Schedule& sched) const;
    void evaluateSafetyInterlocks();
    void evaluateOverrideTimeouts();
    void evaluateSchedules();
    void evaluateThermostat();
    void executeHardwareStates();

    void loadSettings();
    void saveSettings();
};

#endif