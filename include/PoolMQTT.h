#ifndef POOL_MQTT_H
#define POOL_MQTT_H

#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "PoolLogic.h"

class PoolMQTT {
public:
    PoolMQTT(PoolLogic& logic);
    
    void begin();
    void loop();
    void publishStatus(); 

private:
    void reconnect();
    void publishDiscovery();
    void handleCommand(char* topic, byte* payload, unsigned int length);
    
    void sendDiscoverySwitch(const char* name, const char* id, const char* state_topic, const char* cmd_topic, const char* deviceJson);
    void sendDiscoveryNumber(const char* name, const char* id, const char* state_topic, const char* cmd_topic, int min_val, int max_val, const char* deviceJson);
    void sendDiscoverySensor(const char* name, const char* id, const char* state_topic, const char* unit, const char* device_class, const char* deviceJson);
    void sendDiscoveryClimate(const char* deviceJson);

    WiFiClient _wifiClient;
    PubSubClient _mqtt;
    PoolLogic& _logic;
    
    unsigned long _lastStatusPublish = 0;
    char _brokerIp[40];
    int _brokerPort;

    int _timeoutSpa = 120;
    int _timeoutHeater = 120;
    int _timeoutLights = 120;
    int _timeoutVacuum = 120;
};

#endif