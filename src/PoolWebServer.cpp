#include "PoolWebServer.h"
#include <ArduinoJson.h>
#include <WiFi.h>

PoolWebServer* globalWebServerInstance = nullptr;

PoolWebServer::PoolWebServer(PoolLogic& poolLogic, PoolNetworkManager& netMgr)
    : _poolLogic(poolLogic), _netMgr(netMgr), _server(80), _mqtt(_wifiClient), _lastMqttPublish(0) {}

void PoolWebServer::begin() {
    globalWebServerInstance = this;
    setupRoutes();
    _server.begin();
    Serial.println("HTTP PoolWebServer started on port 80.");
    _mqtt.setClient(_wifiClient);
    _mqtt.setServer("192.168.1.9", 1883);
    _mqtt.setCallback(mqttCallback); 
}

void PoolWebServer::loop() {
    _server.handleClient();
    if (WiFi.status() == WL_CONNECTED) {
        handleMQTT();
        unsigned long currentMillis = millis();
        if (currentMillis - _lastMqttPublish >= MQTT_PUBLISH_INTERVAL) {
            _lastMqttPublish = currentMillis;
            publishState();
        }
    }
}

void PoolWebServer::sendCORSHeaders() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void PoolWebServer::setupRoutes() {
    // API GET
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    // API POST
    _server.on("/api/mode", HTTP_POST, [this]() { handleSetMode(); });
    _server.on("/api/override", HTTP_POST, [this]() { handleSetOverride(); });
    // CORS Preflight handlers
    _server.on("/api/mode", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/override", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    // Web UI & Captive Portal
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/wifi-setup", HTTP_GET, [this]() { handleWifiSetupGet(); });
    _server.on("/wifi-setup", HTTP_POST, [this]() { handleWifiSetupPost(); });
    _server.onNotFound([this]() { handleNotFound(); });
}

void PoolWebServer::handleGetStatus() {
    JsonDocument doc;
    doc["system_mode"] = static_cast<int>(_poolLogic.getSystemMode());
    doc["water_mode"] = static_cast<int>(_poolLogic.getWaterMode());
    doc["freeze_protection"] = _poolLogic.isFreezeProtectionActive();
    doc["lights_on"] = _poolLogic.isLightsOn();
    doc["vacuum_on"] = _poolLogic.isVacuumOn();
    doc["fountain_on"] = _poolLogic.isFountainOn();
    doc["spa_blower_on"] = _poolLogic.isSpaBlowerOn();
    doc["heater_active"] = _poolLogic.isHeaterActive();
    doc["target_pool_temp"] = _poolLogic.getTargetTempPool();
    doc["target_spa_temp"] = _poolLogic.getTargetTempSpa();
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    sendCORSHeaders();
    _server.send(200, "application/json", jsonResponse);
}

void PoolWebServer::handleSetMode() {
    if (!_server.hasArg("plain")) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));
    if (error) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    if (!doc["system_mode"].isNull()) {
        int mode = doc["system_mode"].as<int>();
        _poolLogic.setSystemMode(static_cast<SystemMode>(mode));
    }
    
    sendCORSHeaders();
    _server.send(200, "application/json", "{\"status\":\"success\"}");
}

void PoolWebServer::handleSetOverride() {
    if (!_server.hasArg("plain")) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Missing JSON body\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));
    if (error) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    
    // Default timeout is 120 minutes if not provided in the JSON
    uint16_t timeout = !doc["timeout"].isNull() ? doc["timeout"].as<uint16_t>() : 120;
    
    if (!doc["water_mode"].isNull()) {
        _poolLogic.overrideWaterMode(static_cast<WaterMode>(doc["water_mode"].as<int>()), timeout);
    }
    if (!doc["lights_on"].isNull()) {
        _poolLogic.overrideLights(doc["lights_on"].as<bool>(), timeout);
    }
    if (!doc["vacuum_on"].isNull()) {
        _poolLogic.overrideVacuum(doc["vacuum_on"].as<bool>(), timeout);
    }
    if (!doc["fountain_on"].isNull()) {
        _poolLogic.overrideFountain(doc["fountain_on"].as<bool>(), timeout);
    }
    if (!doc["spa_blower_on"].isNull()) {
        _poolLogic.overrideSpaBlower(doc["spa_blower_on"].as<bool>(), timeout);
    }
    if (!doc["heater_enable"].isNull()) {
        _poolLogic.overrideHeater(doc["heater_enable"].as<bool>(), timeout);
    }
    
    sendCORSHeaders();
    _server.send(200, "application/json", "{\"status\":\"success\"}");
}

void PoolWebServer::handleSetSettings() {
    sendCORSHeaders();
    _server.send(501, "application/json", "{\"error\":\"Not Implemented\"}");
}

void PoolWebServer::handleRoot() {
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        _server.sendHeader("Location", "/wifi-setup", true);
        _server.send(302, "text/plain", "");
        return;
    }
    _server.send(200, "text/html", "<h1>KinCony Pool Controller</h1><p>API is active.</p>");
}

void PoolWebServer::handleNotFound() {
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        _server.sendHeader("Location", "/wifi-setup", true);
        _server.send(302, "text/plain", "");
        return;
    }
    sendCORSHeaders();
    _server.send(404, "application/json", "{\"error\":\"Not Found\"}");
}

void PoolWebServer::handleWifiSetupGet() {
    String html = R"rawliteral(
    <!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: sans-serif; padding: 20px; text-align: center; background: #f4f4f9; }
      .container { max-width: 400px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
      input { margin: 10px 0; padding: 12px; width: 90%; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box; }
      button { padding: 12px 20px; background: #007BFF; color: white; border: none; border-radius: 5px; width: 90%; font-size: 16px; cursor: pointer; }
    </style>
    </head><body>
    <div class="container">
      <h2>Pool Controller Setup</h2>
      <p>Enter your home Wi-Fi details:</p>
      <form action="/wifi-setup" method="POST">
        <input type="text" name="ssid" placeholder="Wi-Fi Network Name" required><br>
        <input type="password" name="pass" placeholder="Password" required><br>
        <button type="submit">Save & Connect</button>
      </form>
    </div>
    </body></html>
    )rawliteral";
    
    _server.send(200, "text/html", html);
}

void PoolWebServer::handleWifiSetupPost() {
    // The WebServer library inherently returns Arduino Strings
    String ardSsid = _server.arg("ssid");
    String ardPass = _server.arg("pass");
    
    if (ardSsid.length() > 0) {
        String successHtml = "<h2>Credentials Saved!</h2><p>Rebooting to connect to <b>" + ardSsid + "</b>...</p>";
        _server.send(200, "text/html", successHtml);
        
        delay(1000); // Give the ESP32 a second to actually send the HTML to the phone
        
        // Convert to std::string right at the boundary
        std::string stdSsid = ardSsid.c_str();
        std::string stdPass = ardPass.c_str();
        
        _netMgr.setCredentials(stdSsid, stdPass); 
        
        Serial.println("Credentials saved. Restarting ESP32...");
        delay(500); // Brief pause to let serial buffers clear
        ESP.restart(); // Force reboot to apply new network settings
        
    } else {
        _server.send(400, "text/plain", "SSID cannot be empty.");
    }
}

void PoolWebServer::handleMQTT() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    if (!_mqtt.connected()) {
        Serial.println("Attempting MQTT connection...");
        if (_mqtt.connect("KinConyPoolController")) {
            Serial.println("MQTT connected!");
            _mqtt.subscribe("pool/command");
        } else {
            Serial.print("MQTT failed, rc=");
            Serial.print(_mqtt.state());
            Serial.println(" try again in next loop.");
        }
    }
    if (_mqtt.connected()) {
        _mqtt.loop(); 
    }
}

void PoolWebServer::publishState() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!_mqtt.connected()) return;
    
    JsonDocument doc;
    doc["system_mode"] = static_cast<int>(_poolLogic.getSystemMode());
    doc["water_mode"] = static_cast<int>(_poolLogic.getWaterMode());
    doc["lights_on"] = _poolLogic.isLightsOn();
    doc["vacuum_on"] = _poolLogic.isVacuumOn();
    doc["fountain_on"] = _poolLogic.isFountainOn();
    doc["spa_blower_on"] = _poolLogic.isSpaBlowerOn();
    doc["heater_active"] = _poolLogic.isHeaterActive();
    
    String payload;
    serializeJson(doc, payload);
    _mqtt.publish("pool/status", payload.c_str());
}

void PoolWebServer::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!globalWebServerInstance) return;
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
        Serial.println("MQTT JSON Parse Error");
        return;
    }
    
    if (String(topic) == "pool/command") {
        if (!doc["system_mode"].isNull()) {
            globalWebServerInstance->_poolLogic.setSystemMode(static_cast<SystemMode>(doc["system_mode"].as<int>()));
            Serial.println("MQTT Command: System Mode Changed");
        }
        if (!doc["spa_blower_on"].isNull()) {
            globalWebServerInstance->_poolLogic.overrideSpaBlower(doc["spa_blower_on"].as<bool>(), 120);
            Serial.println("MQTT Command: Spa Blower Override");
        }
        
        globalWebServerInstance->publishState();
    }
}