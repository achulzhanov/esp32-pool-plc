#include "PoolMQTT.h"
#include <string>

// Scoped instance for the callback
static PoolMQTT* globalMqttInstance = nullptr;

PoolMQTT::PoolMQTT(PoolLogic& logic) : _logic(logic), _mqtt(_wifiClient) {
    globalMqttInstance = this;
}

void PoolMQTT::begin() {
    // Expand the library's internal buffer to handle large HA JSON payloads
    _mqtt.setBufferSize(1024);
    Preferences prefs;
    prefs.begin("pool_net", true); 
    std::string ip = prefs.getString("mqtt_broker", "").c_str();
    _brokerPort = prefs.getInt("mqtt_port", 1883);
    prefs.end();
    if (!ip.empty()) {
        strncpy(_brokerIp, ip.c_str(), sizeof(_brokerIp));
        _mqtt.setServer(_brokerIp, _brokerPort);
    } else {
        Serial.println("MQTT Broker IP not set in NVS!");
    }
    _mqtt.setCallback([](char* topic, byte* payload, unsigned int length) {
        if (globalMqttInstance) globalMqttInstance->handleCommand(topic, payload, length);
    });
}

void PoolMQTT::loop() {
    if (WiFi.status() != WL_CONNECTED || strlen(_brokerIp) == 0) return;
    if (!_mqtt.connected()) {
        reconnect();
    } else {
        _mqtt.loop();
        // Push state every 15 seconds to ensure HA is perfectly synced
        if (millis() - _lastStatusPublish > 15000) {
            publishStatus();
            _lastStatusPublish = millis();
        }
    }
}

void PoolMQTT::reconnect() {
    Serial.println("Attempting MQTT connection...");
    // LWT: If ESP drops offline, HA instantly marks entities as "Unavailable"
    if (_mqtt.connect("KinConyPoolController", NULL, NULL, "pool/availability", 0, true, "offline")) {
        Serial.println("MQTT connected!");
        _mqtt.publish("pool/availability", "online", true);
        _mqtt.subscribe("pool/+/set");    
        publishDiscovery(); 
        publishStatus();    
    }
}

void PoolMQTT::handleCommand(char* topic, byte* payload, unsigned int length) {
    std::string top = topic;
    std::string msg(reinterpret_cast<const char*>(payload), length);
    // --- TIMEOUT SLIDERS ---
    if (top == "pool/spa_timeout/set") {
        _timeoutSpa = atoi(msg.c_str());
    } else if (top == "pool/heater_timeout/set") {
        _timeoutHeater = atoi(msg.c_str());
    } else if (top == "pool/lights_timeout/set") {
        _timeoutLights = atoi(msg.c_str());
    } else if (top == "pool/vacuum_timeout/set") {
        _timeoutVacuum = atoi(msg.c_str());
    } 
    // --- SWITCHES ---
    else if (top == "pool/spa_mode/set") {
        _logic.overrideWaterMode(msg == "ON" ? WaterMode::SPA : WaterMode::POOL, _timeoutSpa);
    } else if (top == "pool/lights/set") {
        _logic.overrideLights(msg == "ON", _timeoutLights);
    } else if (top == "pool/vacuum/set") {
        _logic.overrideVacuum(msg == "ON", _timeoutVacuum);
    } else if (top == "pool/jets/set") {
        // Spa jets inherit the remaining time of the Spa Mode via PoolLogic, so we just send 0 or a large default.
        _logic.overrideSpaBlower(msg == "ON", _timeoutSpa); 
    } 
    // --- CLIMATE (THERMOSTAT) ---
    else if (top == "pool/heater_mode/set") {
        // HA sends "heat" or "off"
        _logic.overrideHeater(msg == "heat", _timeoutHeater);
    } else if (top == "pool/target_temp/set") {
        float target = atof(msg.c_str());
        if (_logic.getWaterMode() == WaterMode::SPA) {
            _logic.setTargetTempSpa(target);
        } else {
            _logic.setTargetTempPool(target);
        }
    }
    // Immediately push new state to prevent UI bounce
    publishStatus();
}

void PoolMQTT::publishStatus() {
    if (!_mqtt.connected()) return;
    // Switches
    _mqtt.publish("pool/spa_mode/state", _logic.getWaterMode() == WaterMode::SPA ? "ON" : "OFF");
    _mqtt.publish("pool/lights/state", _logic.isLightsOn() ? "ON" : "OFF");
    _mqtt.publish("pool/vacuum/state", _logic.isVacuumOn() ? "ON" : "OFF");
    _mqtt.publish("pool/jets/state", _logic.isSpaBlowerOn() ? "ON" : "OFF");
    // Timeouts
    _mqtt.publish("pool/spa_timeout/state", std::to_string(_timeoutSpa).c_str());
    _mqtt.publish("pool/heater_timeout/state", std::to_string(_timeoutHeater).c_str());
    _mqtt.publish("pool/lights_timeout/state", std::to_string(_timeoutLights).c_str());
    _mqtt.publish("pool/vacuum_timeout/state", std::to_string(_timeoutVacuum).c_str());
    // Climate (Thermostat)
    _mqtt.publish("pool/heater_mode/state", _logic.isHeaterEnabled() ? "heat" : "off");
    float targetTemp = (_logic.getWaterMode() == WaterMode::SPA) ? _logic.getTargetTempSpa() : _logic.getTargetTempPool();
    char tempBuf[16];
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", targetTemp);
    _mqtt.publish("pool/target_temp/state", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", _logic.getWaterTemp());
    _mqtt.publish("pool/temp_water/state", tempBuf);
    snprintf(tempBuf, sizeof(tempBuf), "%.1f", _logic.getAirTemp());
    _mqtt.publish("pool/temp_air/state", tempBuf);
    _mqtt.publish("pool/availability", "online");
}

void PoolMQTT::publishDiscovery() {
    const char* deviceJson = "\"device\":{\"identifiers\":[\"kincony_pool_01\"],\"name\":\"Pool Controller\",\"manufacturer\":\"Custom\"}";
    // Switches
    sendDiscoverySwitch("Spa Mode", "spa_mode", "pool/spa_mode/state", "pool/spa_mode/set", deviceJson);
    sendDiscoverySwitch("Pool Lights", "lights", "pool/lights/state", "pool/lights/set", deviceJson);
    sendDiscoverySwitch("Vacuum", "vacuum", "pool/vacuum/state", "pool/vacuum/set", deviceJson);
    sendDiscoverySwitch("Spa Jets", "jets", "pool/jets/state", "pool/jets/set", deviceJson);
    // Timeout Sliders (15 mins to 240 mins)
    sendDiscoveryNumber("Spa Timeout", "spa_timeout", "pool/spa_timeout/state", "pool/spa_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Heater Timeout", "heater_timeout", "pool/heater_timeout/state", "pool/heater_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Lights Timeout", "lights_timeout", "pool/lights_timeout/state", "pool/lights_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Vacuum Timeout", "vacuum_timeout", "pool/vacuum_timeout/state", "pool/vacuum_timeout/set", 15, 240, deviceJson);
    // Sensors
    sendDiscoverySensor("Air Temp", "temp_air", "pool/temp_air/state", "°F", "temperature", deviceJson);
    // Climate Entity (Thermostat)
    sendDiscoveryClimate(deviceJson);
}

void PoolMQTT::sendDiscoverySwitch(const char* name, const char* id, const char* state_topic, const char* cmd_topic, const char* deviceJson) {
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/switch/pool/%s/config", id);
    std::string payload = "{\"name\":\"" + std::string(name) + "\",\"unique_id\":\"pool_" + std::string(id) + "\",\"stat_t\":\"" + std::string(state_topic) + "\",\"cmd_t\":\"" + std::string(cmd_topic) + "\",\"avty_t\":\"pool/availability\"," + std::string(deviceJson) + "}";
    _mqtt.publish(topic, payload.c_str(), true);
}

void PoolMQTT::sendDiscoveryNumber(const char* name, const char* id, const char* state_topic, const char* cmd_topic, int min_val, int max_val, const char* deviceJson) {
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/number/pool/%s/config", id);
    std::string payload = "{\"name\":\"" + std::string(name) + "\",\"unique_id\":\"pool_" + std::string(id) + "\",\"stat_t\":\"" + std::string(state_topic) + "\",\"cmd_t\":\"" + std::string(cmd_topic) + "\",\"min\":" + std::to_string(min_val) + ",\"max\":" + std::to_string(max_val) + ",\"step\":15,\"unit_of_measurement\":\"min\",\"mode\":\"slider\",\"avty_t\":\"pool/availability\"," + std::string(deviceJson) + "}";
    _mqtt.publish(topic, payload.c_str(), true);
}

void PoolMQTT::sendDiscoverySensor(const char* name, const char* id, const char* state_topic, const char* unit, const char* device_class, const char* deviceJson) {
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/pool/%s/config", id);
    std::string payload = "{\"name\":\"" + std::string(name) + "\",\"unique_id\":\"pool_" + std::string(id) + "\",\"stat_t\":\"" + std::string(state_topic) + "\",\"unit_of_measurement\":\"" + std::string(unit) + "\",\"device_class\":\"" + std::string(device_class) + "\",\"avty_t\":\"pool/availability\"," + std::string(deviceJson) + "}";
    _mqtt.publish(topic, payload.c_str(), true);
}

void PoolMQTT::sendDiscoveryClimate(const char* deviceJson) {
    const char* topic = "homeassistant/climate/pool/heater/config";
    std::string payload = "{\"name\":\"Pool Heater\",\"unique_id\":\"pool_heater_climate\",\"curr_temp_t\":\"pool/temp_water/state\",\"temp_stat_t\":\"pool/target_temp/state\",\"temp_cmd_t\":\"pool/target_temp/set\",\"mode_stat_t\":\"pool/heater_mode/state\",\"mode_cmd_t\":\"pool/heater_mode/set\",\"modes\":[\"off\",\"heat\"],\"min_temp\":40,\"max_temp\":104,\"temp_step\":1.0,\"avty_t\":\"pool/availability\"," + std::string(deviceJson) + "}";
    _mqtt.publish(topic, payload.c_str(), true);
}