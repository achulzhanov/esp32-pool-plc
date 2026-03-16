#ifndef POOL_WEB_SERVER_H
#define POOL_WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <NetworkClient.h>
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

};

#endif