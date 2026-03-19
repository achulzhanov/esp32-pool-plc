#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "KinConyPLC.h"
#include "PoolLogic.h"
#include "PoolNetworkManager.h"
#include "PoolWebServer.h"
#include "PoolMQTT.h"

// Initialization order is important here
KinConyPLC plc;
PoolNetworkManager netMgr;
PoolLogic logic(plc, netMgr);
PoolWebServer webServer(logic, netMgr);
PoolMQTT mqttClient(logic);

// Watchdog timeout in seconds
#define WDT_TIMEOUT 5 

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("--- Booting Pool Controller ---");

    // HARDWARE SAFETY INITIALIZATION (ESP-IDF v5+ API)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Monitor all cores
        .trigger_panic = true                            // Hard reset on timeout
    };
    esp_task_wdt_reconfigure(&wdt_config);
    esp_task_wdt_add(NULL);

    plc.begin(); 
    netMgr.begin();
    logic.begin();
    webServer.begin();
    mqttClient.begin();
    
    Serial.println("Boot sequence complete.");
}

void loop() {
    esp_task_wdt_reset(); 

    logic.loop();
    netMgr.loop();
    webServer.loop();
    mqttClient.loop();
}