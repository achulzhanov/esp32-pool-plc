#ifndef POOL_LOGGING_H
#define POOL_LOGGING_H

#include <string>

class PoolLogging {
public:
    static constexpr const char* SUCCESS = "success";
    static constexpr const char* ERR_TIMEOUT_INVALID = "error: timeout value out of range";
    static constexpr const char* ERR_SERVICE_LOCKOUT = "error: system locked in service mode";
    static constexpr const char* ERR_OVERRIDE_CONFLICT = "error: conflicting override active";
    static constexpr const char* ERR_RELAY_STATE = "error: relay state mismatch";
    static constexpr const char* ERR_HEATER_INTERLOCK = "error: heater interlock active";
};

#endif
