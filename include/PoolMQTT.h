#ifndef POOL_MQTT_H
#define POOL_MQTT_H

#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "PoolLogic.h"
#include "PoolNetworkManager.h"

class PoolMQTT {
public:
    PoolMQTT(PoolLogic& logic, PoolNetworkManager& netMgr);
    
    void begin();
    void loop();
    void publishStatus();

    // Connection diagnostics surfaced by the Web UI via /api/status.
    bool isConnected();
    int getState();

private:
    void reconnect();
    void publishDiscovery();
    void handleCommand(char* topic, byte* payload, unsigned int length);
    
    void sendDiscoverySwitch(const char* name, const char* id, const char* state_topic, const char* cmd_topic, const char* deviceJson);
    void sendDiscoveryNumber(const char* name, const char* id, const char* state_topic, const char* cmd_topic, int min_val, int max_val, const char* deviceJson);
    void sendDiscoverySensor(const char* name, const char* id, const char* state_topic, const char* unit, const char* device_class, const char* deviceJson);
    void sendDiscoveryClimate(const char* name, const char* id, int min_temp, int max_temp, const char* deviceJson, const char* avty_topic);

    // WiFiClient is a typedef for NetworkClient in the ESP32 Arduino core, so the
    // same socket works over the W5500 Ethernet link as well as over Wi-Fi.
    WiFiClient _wifiClient;
    PubSubClient _mqtt;
    PoolLogic& _logic;
    PoolNetworkManager& _netMgr;
    
    unsigned long _lastStatusPublish = 0;
    unsigned long _lastReconnectAttempt = 0;
    char _brokerIp[40];
    int _brokerPort;
    char _brokerUser[64];
    char _brokerPass[64];

    int _timeoutSpa = 120;
    int _timeoutHeater = 120;
    int _timeoutLights = 120;
    int _timeoutVacuum = 120;
    int _timeoutFountain = 120;
};

#endif