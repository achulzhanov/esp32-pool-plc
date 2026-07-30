#include "PoolMQTT.h"
#include <string>

// Scoped instance for the callback
static PoolMQTT* globalMqttInstance = nullptr;

// Human-readable form of PubSubClient::state(), so a rejected login is obvious
// on the serial console instead of a bare number.
static const char* mqttStateText(int state) {
    switch (state) {
        case -4: return "connection timeout";
        case -3: return "connection lost";
        case -2: return "cannot reach broker";
        case -1: return "disconnected";
        case 0:  return "connected";
        case 1:  return "bad protocol version";
        case 2:  return "client ID rejected";
        case 3:  return "broker unavailable";
        case 4:  return "bad username or password";
        case 5:  return "not authorized";
        default: return "unknown";
    }
}

PoolMQTT::PoolMQTT(PoolLogic& logic, PoolNetworkManager& netMgr)
    : _mqtt(_wifiClient), _logic(logic), _netMgr(netMgr) {
    globalMqttInstance = this;
    _brokerIp[0] = '\0';
    _brokerUser[0] = '\0';
    _brokerPass[0] = '\0';
}

void PoolMQTT::begin() {
    _mqtt.setBufferSize(1024);
    // Cap the CONNACK/read wait well under the 5s hardware watchdog. A broker that
    // rejects the login by silently dropping the socket would otherwise block the
    // main loop for the PubSubClient default of 15s and panic-reset the board.
    _mqtt.setSocketTimeout(2);

    Preferences prefs;
    prefs.begin("pool_net", true);
    // snprintf (rather than strncpy) always null-terminates, even on overflow.
    snprintf(_brokerIp, sizeof(_brokerIp), "%s", prefs.getString("mqtt_broker", "").c_str());
    snprintf(_brokerUser, sizeof(_brokerUser), "%s", prefs.getString("mqtt_user", "").c_str());
    snprintf(_brokerPass, sizeof(_brokerPass), "%s", prefs.getString("mqtt_pass", "").c_str());
    _brokerPort = prefs.getInt("mqtt_port", 1883);
    prefs.end();

    if (strlen(_brokerIp) > 0) {
        _mqtt.setServer(_brokerIp, _brokerPort);
        Serial.printf("MQTT broker %s:%d, auth: %s\n", _brokerIp, _brokerPort,
                      strlen(_brokerUser) > 0 ? _brokerUser : "anonymous");
    } else {
        Serial.println("MQTT Broker IP not set in NVS!");
    }

    _mqtt.setCallback([](char* topic, byte* payload, unsigned int length) {
        if (globalMqttInstance) globalMqttInstance->handleCommand(topic, payload, length);
    });
}

void PoolMQTT::loop() {
    // Ask the network manager rather than WiFi.status(): the controller may hold a
    // routable IP over Ethernet, over Wi-Fi, or (after a failover) both.
    if (!_netMgr.isConnected() || strlen(_brokerIp) == 0) return;
    if (!_mqtt.connected()) {
        // Throttle retries. A broker that refuses our credentials will reject every
        // attempt, and hammering it once per loop iteration would starve the rest
        // of the pool logic of CPU time.
        if (millis() - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = millis();
            reconnect();
        }
    } else {
        _mqtt.loop();
        if (millis() - _lastStatusPublish > 15000) {
            publishStatus();
            _lastStatusPublish = millis();
        }
    }
}

bool PoolMQTT::isConnected() {
    return _mqtt.connected();
}

int PoolMQTT::getState() {
    return _mqtt.state();
}

void PoolMQTT::reconnect() {
    // PubSubClient reads a NULL username as "send no credentials", which keeps
    // working against brokers that still allow anonymous connections.
    const char* user = strlen(_brokerUser) > 0 ? _brokerUser : NULL;
    const char* pass = strlen(_brokerPass) > 0 ? _brokerPass : NULL;

    Serial.printf("Attempting MQTT connection to %s:%d as %s...\n",
                  _brokerIp, _brokerPort, user ? user : "anonymous");
    if (_mqtt.connect("KinConyPoolController", user, pass, "pool/availability", 0, true, "offline")) {
        Serial.println("MQTT connected!");
        // We push global availability FIRST so the specific climate entities don't glitch
        _mqtt.publish("pool/availability", "online", true);
        
        _mqtt.subscribe("pool/+/+/set"); 
        _mqtt.subscribe("pool/+/set");    
        
        publishDiscovery();
        publishStatus();
    } else {
        Serial.printf("MQTT connect failed: %s (state %d)\n",
                      mqttStateText(_mqtt.state()), _mqtt.state());
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
    } else if (top == "pool/fountain_timeout/set") {
        _timeoutFountain = atoi(msg.c_str());
    }
    
    // --- SWITCHES ---
    else if (top == "pool/spa_mode/set") {
        _logic.overrideWaterMode(msg == "ON" ? WaterMode::SPA : WaterMode::POOL, _timeoutSpa);
    } else if (top == "pool/lights/set") {
        _logic.overrideLights(msg == "ON", _timeoutLights);
    } else if (top == "pool/vacuum/set") {
        _logic.overrideVacuum(msg == "ON", _timeoutVacuum);
    } else if (top == "pool/fountain/set") {
        _logic.overrideFountain(msg == "ON", _timeoutFountain);
    } else if (top == "pool/jets/set") {
        _logic.overrideSpaBlower(msg == "ON", _timeoutSpa); 
    } else if (top == "pool/heater_enable/set") {
        // The new dedicated Pool Heater switch
        _logic.overrideHeater(msg == "ON", _timeoutHeater); 
    }
    
    // --- POOL THERMOSTAT (Setpoint Only Now) ---
    else if (top == "pool/pool_heater/target_temp/set") {
        _logic.setTargetTempPool(atof(msg.c_str()));
    }
    
    // --- SPA THERMOSTAT (Setpoint Only Now) ---
    else if (top == "pool/spa_heater/target_temp/set") {
        _logic.setTargetTempSpa(atof(msg.c_str()));
    }

    publishStatus();
}

void PoolMQTT::publishStatus() {
    if (!_mqtt.connected()) return;
    char buf[16];
    
    // Switches
    _mqtt.publish("pool/spa_mode/state", _logic.getWaterMode() == WaterMode::SPA ? "ON" : "OFF");
    _mqtt.publish("pool/heater_enable/state", _logic.isHeaterEnabled() ? "ON" : "OFF");
    _mqtt.publish("pool/lights/state", _logic.isLightsOn() ? "ON" : "OFF");
    _mqtt.publish("pool/vacuum/state", _logic.isVacuumOn() ? "ON" : "OFF");
    _mqtt.publish("pool/fountain/state", _logic.isFountainOn() ? "ON" : "OFF");
    _mqtt.publish("pool/jets/state", _logic.isSpaBlowerOn() ? "ON" : "OFF");
    
    // Timeouts Setpoints
    _mqtt.publish("pool/spa_timeout/state", std::to_string(_timeoutSpa).c_str());
    _mqtt.publish("pool/heater_timeout/state", std::to_string(_timeoutHeater).c_str());
    _mqtt.publish("pool/lights_timeout/state", std::to_string(_timeoutLights).c_str());
    _mqtt.publish("pool/vacuum_timeout/state", std::to_string(_timeoutVacuum).c_str());
    _mqtt.publish("pool/fountain_timeout/state", std::to_string(_timeoutFountain).c_str());

    // Live Countdowns
    _mqtt.publish("pool/spa_time_remaining/state", std::to_string(_logic.getSpaTimeRemaining()).c_str());
    _mqtt.publish("pool/heater_time_remaining/state", std::to_string(_logic.getHeaterTimeRemaining()).c_str());
    _mqtt.publish("pool/lights_time_remaining/state", std::to_string(_logic.getLightsTimeRemaining()).c_str());
    _mqtt.publish("pool/vacuum_time_remaining/state", std::to_string(_logic.getVacuumTimeRemaining()).c_str());
    _mqtt.publish("pool/fountain_time_remaining/state", std::to_string(_logic.getFountainTimeRemaining()).c_str());

    // Dynamic Thermostat Availability Magic
    if (_logic.getWaterMode() == WaterMode::SPA) {
        _mqtt.publish("pool/pool_heater/availability", "offline", true);
        _mqtt.publish("pool/spa_heater/availability", "online", true);
    } else {
        _mqtt.publish("pool/pool_heater/availability", "online", true);
        _mqtt.publish("pool/spa_heater/availability", "offline", true);
    }

    // Pool Thermostat Update
    _mqtt.publish("pool/pool_heater/mode/state", (_logic.getWaterMode() == WaterMode::POOL && _logic.isHeaterEnabled()) ? "heat" : "off");
    snprintf(buf, sizeof(buf), "%.1f", _logic.getTargetTempPool());
    _mqtt.publish("pool/pool_heater/target_temp/state", buf);
    
    // Spa Thermostat Update
    _mqtt.publish("pool/spa_heater/mode/state", (_logic.getWaterMode() == WaterMode::SPA) ? "heat" : "off");
    snprintf(buf, sizeof(buf), "%.1f", _logic.getTargetTempSpa());
    _mqtt.publish("pool/spa_heater/target_temp/state", buf);

    // Global Temps
    snprintf(buf, sizeof(buf), "%.1f", _logic.getWaterTemp());
    _mqtt.publish("pool/temp_water/state", buf);
    snprintf(buf, sizeof(buf), "%.1f", _logic.getAirTemp());
    _mqtt.publish("pool/temp_air/state", buf);
    
    // We do NOT publish to the global pool/availability here, it's handled by LWT and reconnect()
}

void PoolMQTT::publishDiscovery() {
    const char* deviceJson = "\"device\":{\"identifiers\":[\"kincony_pool_01\"],\"name\":\"Pool Controller\",\"manufacturer\":\"Custom\"}";
    
    // Switches
    sendDiscoverySwitch("Spa Mode", "spa_mode", "pool/spa_mode/state", "pool/spa_mode/set", deviceJson);
    sendDiscoverySwitch("Pool Heater Enable", "heater_enable", "pool/heater_enable/state", "pool/heater_enable/set", deviceJson);
    sendDiscoverySwitch("Pool Lights", "lights", "pool/lights/state", "pool/lights/set", deviceJson);
    sendDiscoverySwitch("Vacuum", "vacuum", "pool/vacuum/state", "pool/vacuum/set", deviceJson);
    sendDiscoverySwitch("Fountain", "fountain", "pool/fountain/state", "pool/fountain/set", deviceJson);
    sendDiscoverySwitch("Spa Jets", "jets", "pool/jets/state", "pool/jets/set", deviceJson);
    
    // Timeout Sliders 
    sendDiscoveryNumber("Spa Timeout", "spa_timeout", "pool/spa_timeout/state", "pool/spa_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Heater Timeout", "heater_timeout", "pool/heater_timeout/state", "pool/heater_timeout/set", 15, 720, deviceJson); 
    sendDiscoveryNumber("Lights Timeout", "lights_timeout", "pool/lights_timeout/state", "pool/lights_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Vacuum Timeout", "vacuum_timeout", "pool/vacuum_timeout/state", "pool/vacuum_timeout/set", 15, 240, deviceJson);
    sendDiscoveryNumber("Fountain Timeout", "fountain_timeout", "pool/fountain_timeout/state", "pool/fountain_timeout/set", 15, 240, deviceJson);
    
    // Read-only Sensors
    sendDiscoverySensor("Air Temp", "temp_air", "pool/temp_air/state", "°F", "temperature", deviceJson);
    sendDiscoverySensor("Spa Time Remaining", "spa_time_remaining", "pool/spa_time_remaining/state", "min", "duration", deviceJson);
    sendDiscoverySensor("Heater Time Remaining", "heater_time_remaining", "pool/heater_time_remaining/state", "min", "duration", deviceJson);
    sendDiscoverySensor("Lights Time Remaining", "lights_time_remaining", "pool/lights_time_remaining/state", "min", "duration", deviceJson);
    sendDiscoverySensor("Vacuum Time Remaining", "vacuum_time_remaining", "pool/vacuum_time_remaining/state", "min", "duration", deviceJson);
    sendDiscoverySensor("Fountain Time Remaining", "fountain_time_remaining", "pool/fountain_time_remaining/state", "min", "duration", deviceJson);
    
    // Climate Entities (Thermostats with their dedicated availability topics)
    sendDiscoveryClimate("Pool Thermostat", "pool_heater", 65, 85, deviceJson, "pool/pool_heater/availability");
    sendDiscoveryClimate("Spa Thermostat", "spa_heater", 85, 104, deviceJson, "pool/spa_heater/availability");
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

void PoolMQTT::sendDiscoveryClimate(const char* name, const char* id, int min_temp, int max_temp, const char* deviceJson, const char* avty_topic) {
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/climate/pool/%s/config", id);
    
    // We use HA's availability list so it requires BOTH the global ESP32 status AND the specific mode status to be online.
    std::string payload = "{\"name\":\"" + std::string(name) + "\",\"unique_id\":\"pool_" + std::string(id) + 
        "\",\"curr_temp_t\":\"pool/temp_water/state\"," +
        "\"temp_stat_t\":\"pool/" + std::string(id) + "/target_temp/state\"," +
        "\"temp_cmd_t\":\"pool/" + std::string(id) + "/target_temp/set\"," +
        "\"mode_stat_t\":\"pool/" + std::string(id) + "/mode/state\"," +
        "\"modes\":[\"off\",\"heat\"],\"min_temp\":" + std::to_string(min_temp) + 
        ",\"max_temp\":" + std::to_string(max_temp) + ",\"temp_step\":1.0," +
        "\"availability\":[{\"topic\":\"pool/availability\"},{\"topic\":\"" + std::string(avty_topic) + "\"}]," + 
        "\"availability_mode\":\"all\"," + std::string(deviceJson) + "}";
    
    _mqtt.publish(topic, payload.c_str(), true);
}