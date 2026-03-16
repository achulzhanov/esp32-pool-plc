#ifndef POOL_NETWORK_MANAGER_H
#define POOL_NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <Preferences.h>
#include <string>

class PoolNetworkManager {
public:
    PoolNetworkManager();

    void begin();
    void loop();

    bool isConnected();
    bool isEthernet();
    bool isWiFi();
    bool isAPmode();
    
    std::string getSavedSSID();
    void setCredentials(const std::string& ssid, const std::string& password);
    void clearCredentials();
    void reconnectWiFi();

private:
    static constexpr uint8_t PIN_ETH_CLK = 1;
    static constexpr uint8_t PIN_ETH_MOSI = 2;
    static constexpr uint8_t PIN_ETH_MISO = 41;
    static constexpr uint8_t PIN_ETH_CS = 42;
    static constexpr uint8_t PIN_ETH_INT = 43;
    static constexpr uint8_t PIN_ETH_RST = 44;

    Preferences _prefs;

    enum class NetState {
        DISCONNECTED,
        ETH_CONNECTED,
        WIFI_CONNECTED,
        AP_MODE
    };
    NetState _currentState;

    unsigned long _bootTime = 0;
    bool _hasConnectedOnce = false;

    bool attemptEthernet();
    bool attemptWiFi();
    void startCaptivePortal();
    static void networkEventCallback(WiFiEvent_t event);
};

#endif