#include "NetworkManager.h"
#include <SPI.h>
#include <DNSServer.h>
using std::string;

// Static pointer so static hardware callback can access the class variables
static NetworkManager* globalNetMgr = nullptr;

// Captive portal DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

NetworkManager::NetworkManager() : _currentState(NetState::DISCONNECTED) {
    globalNetMgr = this; // Bind instance to global pointer
}

void NetworkManager::begin() {
    WiFi.onEvent(networkEventCallback);
    _prefs.begin("network", false);
    Serial.println("System Boot: Attempting initial network connection...");
    // If no credentials exist, go straight to AP mode
    if (getSavedSSID().empty()) {
        startCaptivePortal();
        return;
    }
    // Network outage - retry connection for 5 minutes before starting AP
    unsigned long startAttemptTime = millis();
    while (millis() - startAttemptTime < 300000) {
        if (attemptEthernet() || attemptWiFi()) {
            return;
        }
        Serial.println("Reconnect unsuccessful. Retrying...");
        delay(5000);
    }
    // Assume password changed or network is down, start AP mode
    startCaptivePortal();
}

void NetworkManager::loop() {
    // If broadcasting captive portal, continuously process DNS requests to
    // automatically open login page
    if (_currentState == NetState::AP_MODE) {
        dnsServer.processNextRequest();
    }
    // If disconnected, retry connection endlessly (unless rebooted)
    if (_currentState == NetState::DISCONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.println("Network connection lost. Attempting to reconnect...");
            if (attemptEthernet()) {
                Serial.println("Reconnected successfully via Ethernet.");
                return;
            }
            if (attemptWiFi()) {
                Serial.println("Reconnected successfully via WiFi.");
                return;
            }
            Serial.println("Reconnect unsuccessful. Retrying in 10 seconds...");
        }
    }
}

bool NetworkManager::isConnected() {
    return (_currentState == NetState::ETH_CONNECTED || _currentState == NetState::WIFI_CONNECTED);
}

bool NetworkManager::isEthernet() {
    return (_currentState == NetState::ETH_CONNECTED);
}

bool NetworkManager::isWiFi() {
    return (_currentState == NetState::WIFI_CONNECTED);
}

bool NetworkManager::isAPmode() {
    return (_currentState == NetState::AP_MODE);
}

string NetworkManager::getSavedSSID() {
    // Preferences returns Arduino String, extract c_str() to build std::string
    return string(_prefs.getString("ssid", "").c_str());
}

void NetworkManager::setCredentials(const string& ssid, const string& password) {
    // Convert std::string back Arduino String for Preferences
    _prefs.putString("ssid", String(ssid.c_str()));
    _prefs.putString("pass", String(password.c_str()));
}

void NetworkManager::clearCredentials() {
    _prefs.remove("ssid");
    _prefs.remove("pass");
}

void NetworkManager::reconnectWiFi() {
    if (_currentState == NetState::WIFI_CONNECTED) {
        WiFi.disconnect();
    }
    attemptWiFi();
}

bool NetworkManager::attemptEthernet() {
    Serial.println("Attempting Ethernet connection...");
    // Initialize global SPI bus
    SPI.begin(PIN_ETH_CLK, PIN_ETH_MISO, PIN_ETH_MOSI, PIN_ETH_CS);
    // Start ETH library by passing W5500 PHY type and SPI object
    bool ethStarted = ETH.begin(ETH_PHY_W5500, 1, PIN_ETH_CS, PIN_ETH_INT, PIN_ETH_RST, SPI);
    if (ethStarted) {
        // Give DHCP server time to assign IP
        delay(2000);
        if (ETH.linkUp()) {
            Serial.print("Ethernet connected! IP: ");
            Serial.println(ETH.localIP());
            _currentState = NetState::ETH_CONNECTED;
            return true;
        }
    }
    Serial.println("Ethernet failed to connect.");
    return false;
}

bool NetworkManager::attemptWiFi() {
    string savedSSID = getSavedSSID();
    string savedPass = string(_prefs.getString("pass", "").c_str());
    if (savedSSID.empty()) {
        Serial.println("No WiFi credentials saved.");
        return false;
    }
    Serial.print("Attempting WiFi connection to: ");
    Serial.println(savedSSID.c_str());
    // Switch WiFi to station mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected! IP: ");
        Serial.println(WiFi.localIP());
        _currentState = NetState::WIFI_CONNECTED;
        return true;
    }
    Serial.println("\nWiFi failed to connect.");
    return false;
}

void NetworkManager::startCaptivePortal() {
    Serial.println("Starting Captive Portal (AP)...");
    // Switch WiFi to AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP("PoolControllerSetup");
    // Route all incoming traffic to ESP32 gateway IP
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    _currentState = NetState::AP_MODE;
}

void NetworkManager::networkEventCallback(WiFiEvent_t event) {
    if (!globalNetMgr) return;
    switch (event) {
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("Ethernet disconnected.");
            globalNetMgr->_currentState = NetState::DISCONNECTED;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi disconnected.");
            globalNetMgr->_currentState = NetState::DISCONNECTED;
            break;
        default:
            break;
    }
}