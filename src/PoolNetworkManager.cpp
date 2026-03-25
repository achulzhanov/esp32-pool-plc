#include "PoolNetworkManager.h"
#include <SPI.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>
using std::string;

// Static pointer so static hardware callback can access the class variables
static PoolNetworkManager* globalNetMgr = nullptr;

// Captive portal DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;

PoolNetworkManager::PoolNetworkManager() : _currentState(NetState::DISCONNECTED) {
    globalNetMgr = this; // Bind instance to global pointer
}

void PoolNetworkManager::begin() {
    WiFi.onEvent(networkEventCallback);
    _prefs.begin("network", false);
    _bootTime = millis();
    _hasConnectedOnce = false;
    Serial.println("System Boot: Attempting initial network connection...");
    // If no credentials exist, go straight to AP mode
    if (getSavedSSID().empty()) {
        startCaptivePortal();
        return;
    }
    attemptEthernet();
    attemptWiFi();
}

void PoolNetworkManager::loop() {
    // If broadcasting captive portal, continuously process DNS requests to
    // automatically open login page
    if (_currentState == NetState::AP_MODE) {
        dnsServer.processNextRequest();
    }
    // Start mDNS and OTA exactly once, on first successful IP assignment
    if (!_servicesStarted && isConnected()) {
        beginNetworkServices();
    }
    if (_servicesStarted) {
        ArduinoOTA.handle();
    }
    if (_currentState == NetState::DISCONNECTED) {
        // 5-minute initial boot grace period
        if (!_hasConnectedOnce && (millis() - _bootTime > 300000)) {
            Serial.println("Network connection grace period expired. Starting AP mode...");
            startCaptivePortal();
            return;
        }
        // If disconnected, retry connection endlessly (unless rebooted)
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 20000) {
            lastReconnectAttempt = millis();
            Serial.println("Network connection lost. Attempting to reconnect...");
            attemptEthernet();
            attemptWiFi();
        }
    }
}

bool PoolNetworkManager::isConnected() {
    return (_currentState == NetState::ETH_CONNECTED || _currentState == NetState::WIFI_CONNECTED);
}

bool PoolNetworkManager::isEthernet() {
    return (_currentState == NetState::ETH_CONNECTED);
}

bool PoolNetworkManager::isWiFi() {
    return (_currentState == NetState::WIFI_CONNECTED);
}

bool PoolNetworkManager::isAPmode() {
    return (_currentState == NetState::AP_MODE);
}

string PoolNetworkManager::getSavedSSID() {
    // Preferences returns Arduino String, extract c_str() to build std::string
    return string(_prefs.getString("ssid", "").c_str());
}

void PoolNetworkManager::setCredentials(const string& ssid, const string& password) {
    // Convert std::string back Arduino String for Preferences
    _prefs.putString("ssid", String(ssid.c_str()));
    _prefs.putString("pass", String(password.c_str()));
}

void PoolNetworkManager::clearCredentials() {
    _prefs.remove("ssid");
    _prefs.remove("pass");
}

void PoolNetworkManager::reconnectWiFi() {
    if (_currentState == NetState::WIFI_CONNECTED) {
        WiFi.disconnect();
    }
    attemptWiFi();
}

bool PoolNetworkManager::attemptEthernet() {
    Serial.println("Attempting Ethernet connection...");
    // Initialize global SPI bus
    SPI.begin(PIN_ETH_CLK, PIN_ETH_MISO, PIN_ETH_MOSI, PIN_ETH_CS);
    // Start ETH library by passing W5500 PHY type and SPI object
    bool ethStarted = ETH.begin(ETH_PHY_W5500, 1, PIN_ETH_CS, PIN_ETH_INT, PIN_ETH_RST, SPI);
    if (!ethStarted) {
        Serial.println("Ethernet hardware failed to initialize.");
        return false;
    }
    return true;
}

bool PoolNetworkManager::attemptWiFi() {
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
    return true;
}

void PoolNetworkManager::startCaptivePortal() {
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

void PoolNetworkManager::beginNetworkServices() {
    // --- mDNS ---
    // Resolves poolcontrol.local on the network.
    // Both the web admin and the Arduino IDE OTA discovery use this hostname.
    if (MDNS.begin("poolcontrol")) {
        MDNS.addService("http", "tcp", 80); // Advertise the web admin on port 80
        Serial.println("mDNS started: poolcontrol.local");
    } else {
        Serial.println("mDNS start failed.");
    }

    // --- ArduinoOTA ---
    ArduinoOTA.setHostname("poolcontrol"); // Matches mDNS name for IDE discovery

    // Optional: set an OTA password stored in NVS ("ota_pass" key, "network" namespace).
    // Leave the key unset (or empty) to require no password on the local network.
    string otaPass = string(_prefs.getString("ota_pass", "").c_str());
    if (!otaPass.empty()) {
        ArduinoOTA.setPassword(otaPass.c_str());
    }

    ArduinoOTA.onStart([]() {
        // Disable the hardware watchdog before flash write begins.
        // OTA holds the CPU for several seconds; the 5s WDT would otherwise
        // panic-reset the chip mid-flash and brick the firmware.
        esp_task_wdt_delete(NULL);
        Serial.println("OTA: Starting update — watchdog paused.");
    });
    ArduinoOTA.onEnd([]() {
        // Device auto-reboots after this; no need to re-enable the watchdog.
        Serial.println("OTA: Complete. Rebooting...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error [%u]. Rebooting to safe state.\n", error);
        ESP.restart();
    });

    ArduinoOTA.begin();
    _servicesStarted = true;
    Serial.println("ArduinoOTA ready.");
}

void PoolNetworkManager::networkEventCallback(WiFiEvent_t event) {
    if (!globalNetMgr) return;
    switch (event) {
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("Ethernet disconnected.");
            if (globalNetMgr->_currentState != NetState::WIFI_CONNECTED) {
                globalNetMgr->_currentState = NetState::DISCONNECTED;
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi disconnected.");
            if (globalNetMgr->_currentState != NetState::ETH_CONNECTED) {
                globalNetMgr->_currentState = NetState::DISCONNECTED;
            }
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("Ethernet connected! IP: ");
            Serial.println(ETH.localIP());
            globalNetMgr->_currentState = NetState::ETH_CONNECTED;
            globalNetMgr->_hasConnectedOnce = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("WiFi connected! IP: ");
            Serial.println(WiFi.localIP());
            globalNetMgr->_currentState = NetState::WIFI_CONNECTED;
            globalNetMgr->_hasConnectedOnce = true;
            break;
        default:
            break;
    }
}