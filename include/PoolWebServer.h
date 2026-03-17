#ifndef POOL_WEB_SERVER_H
#define POOL_WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "PoolLogic.h"
#include "PoolNetworkManager.h"

class PoolWebServer {
public:
    PoolWebServer(PoolLogic& poolLogic, PoolNetworkManager& netMgr);
    void begin();
    void loop();

private:
    PoolLogic& _poolLogic;
    PoolNetworkManager& _netMgr;

    WebServer _server;
    PubSubClient _mqtt;
    WiFiClient _wifiClient;

    unsigned long _lastMqttPublish;
    const unsigned long MQTT_PUBLISH_INTERVAL = 5000;

    // HTTP routing
    void setupRoutes();
    void sendCORSHeaders();
    // HTML/UI handlers
    void handleRoot();
    void handleNotFound();
    void handleWifiSetupGet();
    void handleWifiSetupPost();
    // REST API endpoint handlers
    void handleGetStatus();
    void handleSetMode();
    void handleSetOverride();
    void handleSetSettings();
    // MQTT helpers
    void handleMQTT();
    void publishState();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
};

#endif