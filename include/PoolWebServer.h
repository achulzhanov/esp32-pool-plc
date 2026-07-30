#ifndef POOL_WEB_SERVER_H
#define POOL_WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include "PoolLogic.h"
#include "PoolNetworkManager.h"
#include "PoolMQTT.h"

class PoolWebServer {
public:
    PoolWebServer(PoolLogic& poolLogic, PoolNetworkManager& netMgr);
    void begin();
    void loop();

    // Optional: lets /api/status report live broker connection state.
    // Wired up in main.cpp after both globals are constructed.
    void setMQTT(PoolMQTT& mqtt);

private:
    PoolLogic& _poolLogic;
    PoolNetworkManager& _netMgr;
    PoolMQTT* _mqtt = nullptr;
    WebServer _server;

    bool _pendingRestart = false;
    unsigned long _restartTime = 0;

    // HTTP routing
    void setupRoutes();
    void sendCORSHeaders();
    // API handlers
    void handleNotFound();
    void handleWifiSetupGet();
    void handleWifiSetupPost();
    void handleSetWiFi();
    void handleGetStatus();
    void handleSetMode();
    void handleSetOverride();
    void handleSetSettings();
    void handleGetSettings();
    void handleServiceCommand();
    void handleReboot();
};

#endif