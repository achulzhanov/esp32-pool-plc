#include "PoolWebServer.h"
#include "WebUI.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <string>

PoolWebServer* globalWebServerInstance = nullptr;

PoolWebServer::PoolWebServer(PoolLogic& poolLogic, PoolNetworkManager& netMgr)
    : _poolLogic(poolLogic), _netMgr(netMgr), _server(80) {}

void PoolWebServer::begin() {
    globalWebServerInstance = this;
    setupRoutes();
    _server.begin();
    Serial.println("HTTP PoolWebServer started on port 80.");
}

void PoolWebServer::loop() {
    _server.handleClient();
    if (_pendingRestart && millis() - _restartTime > 1000) {
        ESP.restart();
    }
}

void PoolWebServer::sendCORSHeaders() {
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void PoolWebServer::setupRoutes() {
    // --- Web UI & Captive Portal ---
    _server.on("/", HTTP_GET, [this]() {
        // 1. Captive Portal Check
        if (_netMgr.isAPmode()) {
            _server.sendHeader("Location", "/wifi-setup", true);
            _server.send(302, "text/plain", "");
            return;
        }
        // 2. Normal Operation
        _server.send(200, "text/html", index_html);
    });
    _server.on("/wifi-setup", HTTP_GET, [this]() { handleWifiSetupGet(); });
    _server.on("/wifi-setup", HTTP_POST, [this]() { handleWifiSetupPost(); });
    // --- API GET ---
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _server.on("/api/settings", HTTP_GET, [this]() { handleGetSettings(); });
    // --- API POST ---
    _server.on("/api/mode", HTTP_POST, [this]() { handleSetMode(); });
    _server.on("/api/override", HTTP_POST, [this]() { handleSetOverride(); });
    _server.on("/api/service", HTTP_POST, [this]() { handleServiceCommand(); });
    _server.on("/api/settings", HTTP_POST, [this]() { handleSetSettings(); });
    _server.on("/api/wifi", HTTP_POST, [this]() { handleSetWiFi(); });
    _server.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
    // --- CORS Preflight handlers ---
    _server.on("/api/mode", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/override", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/service", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/settings", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/wifi", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    _server.on("/api/reboot", HTTP_OPTIONS, [this]() { sendCORSHeaders(); _server.send(204); });
    // --- 404 / Captive Portal Catch-All ---
    _server.onNotFound([this]() { handleNotFound(); });
}

void PoolWebServer::handleSetWiFi() {
    if (!_server.hasArg("plain")) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Missing JSON\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    // ONLY update if both SSID and Password are provided. 
    if (doc.containsKey("ssid") && doc.containsKey("pass")) {
        std::string ssid = doc["ssid"].as<std::string>();
        std::string pass = doc["pass"].as<std::string>();
        if (!ssid.empty() && !pass.empty()) {
            _netMgr.setCredentials(ssid.c_str(), pass.c_str()); 
            Serial.println("Wi-Fi credentials updated via Web UI.");
        }
    }
    // MQTT
    if (doc.containsKey("mqtt_broker")) {
        std::string mqttBroker = doc["mqtt_broker"].as<std::string>();
        int mqttPort = doc["mqtt_port"] | 1883;
        Preferences prefs;
        prefs.begin("pool_net", false); 
        prefs.putString("mqtt_broker", mqttBroker.c_str());
        prefs.putInt("mqtt_port", mqttPort);
        prefs.end();
        Serial.println("MQTT settings updated via Web UI.");
    }
    sendCORSHeaders();
    _server.send(200, "application/json", "{\"status\":\"success\"}");
    // Trigger the deferred restart
    _pendingRestart = true;
    _restartTime = millis();
}

void PoolWebServer::handleGetStatus() {
    JsonDocument doc;
    // Core States
    doc["system_mode"] = static_cast<int>(_poolLogic.getSystemMode());
    doc["water_mode"] = static_cast<int>(_poolLogic.getWaterMode());
    doc["current_time"] = _poolLogic.getCurrentTimeString();
    // Network Info
    doc["ssid"] = WiFi.SSID().c_str();
    doc["ip"] = WiFi.localIP().toString().c_str();
    // Temperatures
    doc["temp_air"] = _poolLogic.getAirTemp();
    doc["temp_water"] = _poolLogic.getWaterTemp();
    doc["target_pool_temp"] = _poolLogic.getTargetTempPool();
    doc["target_spa_temp"] = _poolLogic.getTargetTempSpa();
    doc["freeze_protection"] = _poolLogic.isFreezeProtectionActive();
    // User Intents
    doc["heater_enabled"] = _poolLogic.isHeaterEnabled();
    doc["lights_on"] = _poolLogic.isLightsOn();
    doc["vacuum_on"] = _poolLogic.isVacuumOn();
    doc["fountain_on"] = _poolLogic.isFountainOn();
    doc["spa_blower_on"] = _poolLogic.isSpaBlowerOn();
    // Physical Hardware Truths (God Mode viewing)
    doc["relay_filter_pump"] = _poolLogic.getRelayState(PoolRelay::FilterPump);
    doc["relay_heater_igniter"] = _poolLogic.getRelayState(PoolRelay::HeaterIgniter);
    doc["actuator_intake_spa"] = _poolLogic.getRelayState(PoolRelay::IntakeActuator);
    doc["actuator_return_spa"] = _poolLogic.getRelayState(PoolRelay::ReturnActuator);
    doc["relay_aux_pump"] = _poolLogic.getRelayState(PoolRelay::AuxPump);
    doc["relay_spa_blower"] = _poolLogic.getRelayState(PoolRelay::SpaBlower);
    doc["relay_pool_lights"] = _poolLogic.getRelayState(PoolRelay::PoolLights);
    doc["relay_vacuum_pump"] = _poolLogic.getRelayState(PoolRelay::VacuumPump);
    // MQTT Broker IP
    Preferences prefs;
    prefs.begin("pool_net", true); // true = read-only
    doc["mqtt_broker"] = prefs.getString("mqtt_broker", "").c_str();
    doc["mqtt_port"] = prefs.getInt("mqtt_port", 1883);
    prefs.end();

    std::string jsonResponse;
    serializeJson(doc, jsonResponse);
    sendCORSHeaders();
    _server.send(200, "application/json", jsonResponse.c_str());
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
    std::string result = "success";
    // Cascade through the commands. If one fails a safety interlock, 
    // it will update 'result' and skip the remaining checks.
    if (result == "success" && !doc["water_mode"].isNull()) {
        result = _poolLogic.overrideWaterMode(static_cast<WaterMode>(doc["water_mode"].as<int>()), timeout);
    }
    if (result == "success" && !doc["lights_on"].isNull()) {
        result = _poolLogic.overrideLights(doc["lights_on"].as<bool>(), timeout);
    }
    if (result == "success" && !doc["vacuum_on"].isNull()) {
        result = _poolLogic.overrideVacuum(doc["vacuum_on"].as<bool>(), timeout);
    }
    if (result == "success" && !doc["fountain_on"].isNull()) {
        result = _poolLogic.overrideFountain(doc["fountain_on"].as<bool>(), timeout);
    }
    if (result == "success" && !doc["spa_blower_on"].isNull()) {
        result = _poolLogic.overrideSpaBlower(doc["spa_blower_on"].as<bool>(), timeout);
    }
    if (result == "success" && !doc["heater_enable"].isNull()) {
        result = _poolLogic.overrideHeater(doc["heater_enable"].as<bool>(), timeout);
    }
    sendCORSHeaders();
    if (result == "success") {
        _server.send(200, "application/json", "{\"status\":\"success\"}");
    } else {
        std::string errorJson = std::string("{\"error\":\"") + result + "\"}";
        _server.send(400, "application/json", errorJson.c_str());
    }
}

void PoolWebServer::handleSetSettings() {
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
    // Process Target Temps (Dashboard Tab)
    if (!doc["target_pool_temp"].isNull()) _poolLogic.setTargetTempPool(doc["target_pool_temp"].as<float>());
    if (!doc["target_spa_temp"].isNull()) _poolLogic.setTargetTempSpa(doc["target_spa_temp"].as<float>());
    // Process Calibration (Settings Tab)
    if (!doc["freeze_protect_temp"].isNull()) _poolLogic.setFreezeProtectTemp(doc["freeze_protect_temp"].as<float>());
    if (!doc["water_temp_offset"].isNull()) _poolLogic.setWaterTempOffset(doc["water_temp_offset"].as<float>());
    // Process Schedules (Settings Tab)
    if (!doc["sched_filter"].isNull()) {
        Schedule s;
        s.startHour = doc["sched_filter"]["startHour"].as<uint8_t>();
        s.startMin = doc["sched_filter"]["startMin"].as<uint8_t>();
        s.endHour = doc["sched_filter"]["endHour"].as<uint8_t>();
        s.endMin = doc["sched_filter"]["endMin"].as<uint8_t>();
        s.enabled = doc["sched_filter"]["enabled"].as<bool>();
        _poolLogic.setScheduleFilter(s);
    }
    if (!doc["sched_vacuum"].isNull()) {
        Schedule s;
        s.startHour = doc["sched_vacuum"]["startHour"].as<uint8_t>();
        s.startMin = doc["sched_vacuum"]["startMin"].as<uint8_t>();
        s.endHour = doc["sched_vacuum"]["endHour"].as<uint8_t>();
        s.endMin = doc["sched_vacuum"]["endMin"].as<uint8_t>();
        s.enabled = doc["sched_vacuum"]["enabled"].as<bool>();
        _poolLogic.setScheduleVacuum(s);
    }
    if (!doc["sched_lights"].isNull()) {
        Schedule s;
        s.startHour = doc["sched_lights"]["startHour"].as<uint8_t>();
        s.startMin = doc["sched_lights"]["startMin"].as<uint8_t>();
        s.endHour = doc["sched_lights"]["endHour"].as<uint8_t>();
        s.endMin = doc["sched_lights"]["endMin"].as<uint8_t>();
        s.enabled = doc["sched_lights"]["enabled"].as<bool>();
        _poolLogic.setScheduleLights(s);
    }
    sendCORSHeaders();
    _server.send(200, "application/json", "{\"status\":\"success\"}");
}

void PoolWebServer::handleGetSettings() {
    JsonDocument doc;
    
    // Temperatures & Calibration
    doc["target_pool_temp"] = _poolLogic.getTargetTempPool();
    doc["target_spa_temp"] = _poolLogic.getTargetTempSpa();
    doc["freeze_protect_temp"] = _poolLogic.getFreezeProtectTemp();
    doc["water_temp_offset"] = _poolLogic.getWaterTempOffset();
    // Filter Schedule
    JsonObject sf = doc["sched_filter"].to<JsonObject>();
    Schedule schedF = _poolLogic.getScheduleFilter();
    sf["startHour"] = schedF.startHour; sf["startMin"] = schedF.startMin;
    sf["endHour"] = schedF.endHour; sf["endMin"] = schedF.endMin;
    sf["enabled"] = schedF.enabled;
    // Vacuum Schedule
    JsonObject sv = doc["sched_vacuum"].to<JsonObject>();
    Schedule schedV = _poolLogic.getScheduleVacuum();
    sv["startHour"] = schedV.startHour; sv["startMin"] = schedV.startMin;
    sv["endHour"] = schedV.endHour; sv["endMin"] = schedV.endMin;
    sv["enabled"] = schedV.enabled;
    // Lights Schedule
    JsonObject sl = doc["sched_lights"].to<JsonObject>();
    Schedule schedL = _poolLogic.getScheduleLights();
    sl["startHour"] = schedL.startHour; sl["startMin"] = schedL.startMin;
    sl["endHour"] = schedL.endHour; sl["endMin"] = schedL.endMin;
    sl["enabled"] = schedL.enabled;
    std::string response;
    serializeJson(doc, response);
    sendCORSHeaders();
    _server.send(200, "application/json", response.c_str());
}

void PoolWebServer::handleServiceCommand() {
    if (_poolLogic.getSystemMode() != SystemMode::SERVICE) {
        sendCORSHeaders();
        _server.send(403, "application/json", "{\"error\":\"System must be in SERVICE mode.\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _server.arg("plain"));
    if (error) {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }
    if (!doc["relay"].isNull() && !doc["state"].isNull()) {
        int relayIdx = doc["relay"].as<int>();
        bool state = doc["state"].as<bool>();
        std::string result = _poolLogic.setServiceRelay(static_cast<PoolRelay>(relayIdx), state);
        sendCORSHeaders();
        if (result == "success") {
            _server.send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            // Send the specific error message back to the Web UI
            std::string errorJson = std::string("{\"error\":\"") + result + "\"}";
            _server.send(400, "application/json", errorJson.c_str());
        }
    } else {
        sendCORSHeaders();
        _server.send(400, "application/json", "{\"error\":\"Missing relay or state\"}");
    }
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
    const char* html = R"rawliteral(
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
    std::string ssid = _server.arg("ssid").c_str();
    std::string pass = _server.arg("pass").c_str();
    if (!ssid.empty()) {
        std::string successHtml = std::string("<h2>Credentials Saved!</h2><p>Rebooting to connect to <b>") + ssid + "</b>...</p>";
        _server.send(200, "text/html", successHtml.c_str());
        _netMgr.setCredentials(ssid, pass);
        Serial.println("Credentials saved. Restarting ESP32...");
        _pendingRestart = true;
        _restartTime = millis();
    } else {
        _server.send(400, "text/plain", "SSID cannot be empty.");
    }
}

void PoolWebServer::handleReboot() {
    sendCORSHeaders();
    _server.send(200, "application/json", "{\"status\":\"rebooting\"}");
    _pendingRestart = true;
    _restartTime = millis();
}