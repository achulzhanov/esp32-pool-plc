#include <Arduino.h>
#include "KinConyPLC.h"
#include "PoolNetworkManager.h"
#include "PoolLogic.h"
#include "PoolWebServer.h"

// --- Object Instantiation Order is Critical Here ---

// 1. Hardware layer first
KinConyPLC plc;

// 2. Network layer second
PoolNetworkManager netMgr; 

// 3. Logic layer (requires Hardware and Network)
PoolLogic poolLogic(plc, netMgr);

// 4. Web/API layer (requires Logic and Network)
PoolWebServer webServer(poolLogic, netMgr);

unsigned long lastHeartbeat = 0;

void setup() {
    Serial.begin(115200);
    delay(2000); // Give serial monitor time to connect
    Serial.println("\n--- KinCony Pool PLC Booting ---");

    // 1. Initialize hardware layer first
    plc.begin();

    // 2. Initialize logic hardware (relays, sensors)
    poolLogic.begin();

    // 3. Initialize Networking (This blocks and launches Captive Portal if no Wi-Fi is saved)
    Serial.println("Initializing Network Manager...");
    netMgr.begin();

    // 4. Initialize Web and MQTT Servers
    Serial.println("Initializing Web & MQTT Servers...");
    webServer.begin();

    Serial.println("Boot Sequence Complete.");
}

void loop() {
    // Tick the core systems
    poolLogic.loop();
    netMgr.loop();
    webServer.loop();

    // --- Unit Test / Heartbeat Output ---
    unsigned long currentMillis = millis();
    if (currentMillis - lastHeartbeat >= 5000) {
        lastHeartbeat = currentMillis;

        Serial.println("\n--- System Heartbeat ---");
        Serial.printf("Wi-Fi Connected: %s\n", netMgr.isConnected() ? "YES" : "NO");
        Serial.printf("System Mode: %d\n", static_cast<int>(poolLogic.getSystemMode()));
        Serial.printf("Water Mode: %d\n", static_cast<int>(poolLogic.getWaterMode()));
        Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    }
}