#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Smart Pool Controller</title>
    <style>
        :root {
            --bg: #0b1320;
            --surface: #152336;
            --surface-hover: #1e3047;
            --primary: #00d2ff;
            --text: #ffffff;
            --text-muted: #94a3b8;
            --danger: #ef4444;
            --success: #10b981;
            --actuator: #3b82f6;
            --border: #1e3a5f;
        }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: var(--bg); color: var(--text); margin: 0; padding: 0; padding-bottom: 60px; }
        .container { max-width: 800px; margin: 0 auto; padding: 20px; }
        
        /* Navigation Tabs */
        .nav { display: flex; background: var(--surface); border-bottom: 2px solid var(--border); position: sticky; top: 0; z-index: 100; }
        .nav button { flex: 1; background: transparent; color: var(--text-muted); padding: 15px; border: none; font-size: 1rem; cursor: pointer; border-bottom: 3px solid transparent; border-radius: 0; }
        .nav button.active { color: var(--primary); border-bottom: 3px solid var(--primary); font-weight: bold; background: rgba(0, 210, 255, 0.05); }
        .tab-content { display: none; }
        .tab-content.active { display: block; animation: fadeIn 0.3s; }
        @keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }

        /* Cards & Grids */
        .card { background-color: var(--surface); border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); }
        h2 { margin-top: 0; font-size: 1.2rem; color: var(--primary); border-bottom: 1px solid var(--border); padding-bottom: 10px; }
        h3 { font-size: 1rem; color: var(--text); margin-top: 20px; margin-bottom: 10px; }
        .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
        .stat-box { background: var(--surface-hover); padding: 15px; border-radius: 6px; text-align: center; }
        .stat-val { font-size: 1.5rem; font-weight: bold; margin-top: 5px; color: #fff; }

        /* Controls & Inputs */
        .control-row { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid var(--border); }
        .control-row:last-child { border-bottom: none; }
        .form-group { margin-bottom: 15px; }
        label { display: block; font-size: 0.9rem; color: var(--text-muted); margin-bottom: 5px; }
        input[type="number"], input[type="time"], input[type="text"], input[type="password"] {
            width: 100%; box-sizing: border-box; background: var(--surface-hover); border: 1px solid var(--border); color: white; padding: 10px; border-radius: 4px; font-size: 1rem;
        }
        .schedule-grid { display: grid; grid-template-columns: 1fr 1fr auto; gap: 10px; align-items: end; }

        /* Buttons */
        button.action-btn { background-color: var(--surface-hover); color: white; border: 1px solid var(--border); padding: 10px 20px; border-radius: 4px; font-weight: bold; cursor: pointer; transition: 0.2s; min-width: 80px; }
        button.action-btn:active { transform: scale(0.95); }
        button.active-green { background-color: var(--success); color: #000; border-color: var(--success); }
        button.active-red { background-color: var(--danger); color: #fff; border-color: var(--danger); }
        button.active-actuator { background-color: var(--actuator); color: #fff; border-color: var(--actuator); }
        button.btn-primary { background-color: var(--primary); color: #000; width: 100%; border: none; padding: 12px; font-size: 1rem; margin-top: 10px; }
        button.btn-danger { background-color: transparent; color: var(--danger); border: 1px solid var(--danger); width: 100%; padding: 12px; margin-top: 30px; font-size: 1rem; border-radius: 4px; cursor: pointer; }

        /* Toast Notifications */
        #toast-container { position: fixed; top: 20px; right: 20px; z-index: 1000; }
        .toast { background: var(--surface); color: white; padding: 15px 25px; border-radius: 4px; margin-bottom: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.8); opacity: 0; transition: opacity 0.3s; border: 1px solid var(--border); }
        .toast.error { border-left: 5px solid var(--danger); }
        .toast.success { border-left: 5px solid var(--success); }
    </style>
</head>
<body>
    <div id="toast-container"></div>

    <div class="nav">
        <button id="tab-btn-dashboard" class="active" onclick="switchTab('dashboard')">Dashboard</button>
        <button id="tab-btn-settings" onclick="switchTab('settings')">Settings</button>
        <button id="tab-btn-wifi" onclick="switchTab('wifi')">Network</button>
    </div>

    <div class="container">
        <!-- ================= DASHBOARD TAB ================= -->
        <div id="tab-dashboard" class="tab-content active">
            <div class="card">
                <h2>System Overview</h2>
                <div class="grid-2">
                    <div class="stat-box"><div>System Mode</div><div id="val-mode" class="stat-val">--</div></div>
                    <div class="stat-box"><div>Water Mode</div><div id="val-water" class="stat-val">--</div></div>
                    <div class="stat-box"><div>Water Temp</div><div id="val-temp-w" class="stat-val">-- &deg;F</div></div>
                    <div class="stat-box"><div>Air Temp</div><div id="val-temp-a" class="stat-val">-- &deg;F</div></div>
                </div>
                <p style="text-align: center; color: var(--text-muted); font-size: 0.9rem; margin-top: 15px;">
                    Time: <span id="val-time">--</span> | IP: <span id="val-ip">--</span> | MQTT: <span id="val-mqtt">--</span>
                </p>
            </div>

            <div class="card">
                <div class="control-row">
                    <h2 style="border:none; margin:0;">Target Temperatures</h2>
                </div>
                <div class="grid-2">
                    <div class="form-group">
                        <label>Pool Target (&deg;F)</label>
                        <input type="number" id="target-pool" step="1" max="85">
                    </div>
                    <div class="form-group">
                        <label>Spa Target (&deg;F)</label>
                        <input type="number" id="target-spa" step="1" max="104">
                    </div>
                </div>
                <button class="action-btn btn-primary" onclick="saveTargetTemps()">Save Temperatures</button>
            </div>

            <div class="card">
                <h2 style="border:none; margin:0; padding-bottom: 0;">Water Mode Control</h2>
                <div style="display: flex; gap: 10px; margin-top: 15px;">
                    <button class="action-btn" id="btn-wm-0" style="flex:1;" onclick="setWaterMode(0)">OFF</button>
                    <button class="action-btn" id="btn-wm-1" style="flex:1;" onclick="setWaterMode(1)">POOL</button>
                    <button class="action-btn" id="btn-wm-2" style="flex:1;" onclick="setWaterMode(2)">SPA</button>
                </div>
            </div>

            <div class="card">
                <div class="control-row">
                    <h2 style="border:none; margin:0;">Service Controls</h2>
                    <button id="btn-service" class="action-btn" onclick="toggleServiceMode()">Enable Service Mode</button>
                </div>
                <p style="color: var(--text-muted); font-size: 0.85rem;">Warning: Service mode overrides all schedules and safeties.</p>
                
                <div id="hardware-toggles" style="opacity: 0.5; pointer-events: none; margin-top: 15px;">
                    <div class="control-row"><span>Relay 1: Filter Pump</span><button class="action-btn" id="btn-relay-0" onclick="setRelay(0)">OFF</button></div>
                    <div class="control-row"><span>Relay 2: Aux/Fountain Pump</span><button class="action-btn" id="btn-relay-1" onclick="setRelay(1)">OFF</button></div>
                    <div class="control-row"><span>Relay 3: Vacuum Pump</span><button class="action-btn" id="btn-relay-2" onclick="setRelay(2)">OFF</button></div>
                    <div class="control-row"><span>Relay 4: Spa Blower</span><button class="action-btn" id="btn-relay-3" onclick="setRelay(3)">OFF</button></div>
                    <div class="control-row"><span>Relay 5: Pool Lights</span><button class="action-btn" id="btn-relay-4" onclick="setRelay(4)">OFF</button></div>
                    <div class="control-row"><span>Relay 6: Intake Actuator</span><button class="action-btn" id="btn-relay-5" onclick="setRelay(5)">POOL</button></div>
                    <div class="control-row"><span>Relay 7: Return Actuator</span><button class="action-btn" id="btn-relay-6" onclick="setRelay(6)">SPLIT</button></div>
                    <div class="control-row"><span>Relay 8: Heater Igniter</span><button class="action-btn" id="btn-relay-7" onclick="setRelay(7)">OFF</button></div>
                </div>
            </div>
        </div>

        <!-- ================= SETTINGS TAB ================= -->
        <div id="tab-settings" class="tab-content">
            <div class="card">
                <h2>Automation Schedules</h2>
                
                <h3>Filter Pump</h3>
                <div class="schedule-grid">
                    <div class="form-group"><label>Start</label><input type="time" id="sched-filter-start"></div>
                    <div class="form-group"><label>End</label><input type="time" id="sched-filter-end"></div>
                    <div class="form-group"><button class="action-btn active-green" id="sched-filter-en" onclick="toggleSchedEn(this)">ON</button></div>
                </div>

                <h3>Vacuum Cleaner</h3>
                <div class="schedule-grid">
                    <div class="form-group"><label>Start</label><input type="time" id="sched-vac-start"></div>
                    <div class="form-group"><label>End</label><input type="time" id="sched-vac-end"></div>
                    <div class="form-group"><button class="action-btn active-green" id="sched-vac-en" onclick="toggleSchedEn(this)">ON</button></div>
                </div>

                <h3>Pool Lights</h3>
                <div class="schedule-grid">
                    <div class="form-group"><label>Start</label><input type="time" id="sched-lights-start"></div>
                    <div class="form-group"><label>End</label><input type="time" id="sched-lights-end"></div>
                    <div class="form-group"><button class="action-btn active-green" id="sched-lights-en" onclick="toggleSchedEn(this)">ON</button></div>
                </div>
            </div>

            <div class="card">
                <h2>System Calibration</h2>
                <div class="grid-2">
                    <div class="form-group">
                        <label>Freeze Protect (&deg;F)</label>
                        <input type="number" id="cal-freeze" step="1">
                    </div>
                    <div class="form-group">
                        <label>Water Temp Offset (&deg;F)</label>
                        <input type="number" id="cal-offset" step="0.1">
                    </div>
                </div>
                <button class="action-btn btn-primary" onclick="saveSettings()">Save All Settings</button>
            </div>
        </div>

        <!-- ================= WI-FI / NETWORK TAB ================= -->
        <div id="tab-wifi" class="tab-content">
            <div class="card">
                <h2>Wi-Fi Credentials</h2>
                <p style="color: var(--text-muted); font-size: 0.9rem;">Warning: Changing this will disconnect the controller. Both fields are required to update.</p>
                <div class="form-group">
                    <label>SSID (Network Name)</label>
                    <input type="text" id="wifi-ssid" placeholder="Enter new SSID">
                </div>
                <div class="form-group">
                    <label>Password</label>
                    <input type="password" id="wifi-pass" placeholder="Enter new Password">
                </div>
                <button class="action-btn btn-primary" onclick="saveWiFi()">Update Wi-Fi & Reboot</button>
            </div>

            <div class="card">
                <h2>MQTT Configuration</h2>
                <div class="grid-2">
                    <div class="form-group">
                        <label>Broker IP Address</label>
                        <input type="text" id="mqtt-broker" placeholder="e.g. 192.168.1.9">
                    </div>
                    <div class="form-group">
                        <label>Broker Port</label>
                        <input type="number" id="mqtt-port" placeholder="1883">
                    </div>
                </div>
                <button class="action-btn btn-primary" onclick="saveMQTT()">Update MQTT & Reboot</button>
            </div>

            <!-- Global System Reboot -->
            <button class="btn-danger" onclick="rebootSystem()">Reboot Controller</button>
        </div>
    </div>

    <script>
        let inServiceMode = false;

        // UI Helpers
        function switchTab(tabId) {
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.nav button').forEach(b => b.classList.remove('active'));
            document.getElementById(`tab-${tabId}`).classList.add('active');
            document.getElementById(`tab-btn-${tabId}`).classList.add('active');
            
            if (tabId === 'settings') loadSettings();
            if (tabId === 'wifi') loadNetwork();
        }

        function showToast(msg, isError = false) {
            const container = document.getElementById('toast-container');
            const toast = document.createElement('div');
            toast.className = `toast ${isError ? 'error' : 'success'}`;
            toast.innerText = msg;
            container.appendChild(toast);
            setTimeout(() => toast.style.opacity = '1', 10);
            setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 4000);
        }

        function toggleSchedEn(btn) {
            const isEn = btn.innerText === "ON";
            btn.innerText = isEn ? "OFF" : "ON";
            btn.className = isEn ? "action-btn active-red" : "action-btn active-green";
            btn.dataset.enabled = !isEn;
        }

        function updateRelayButton(btnId, isOn) {
            const btn = document.getElementById(btnId);
            if(btn){
                btn.innerText = isOn ? "ON" : "OFF";
                btn.className = isOn ? "action-btn active-green" : "action-btn";
                btn.dataset.targetState = !isOn; 
            }
        }

        function updateActuatorButton(btnId, isSpa, labelFalse, labelTrue) {
            const btn = document.getElementById(btnId);
            if(btn){
                btn.innerText = isSpa ? labelTrue : labelFalse;
                btn.className = isSpa ? "action-btn active-actuator" : "action-btn";
                btn.dataset.targetState = !isSpa; 
            }
        }

        // API Calls: Dashboard
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();

                document.getElementById('val-time').innerText = data.current_time || "--";
                document.getElementById('val-ip').innerText = data.ip || "--";
                document.getElementById('val-mqtt').innerText = data.mqtt_broker || "Not Set";
                
                document.getElementById('val-temp-w').innerText = data.temp_water < -100 ? "N/A" : data.temp_water.toFixed(1) + " °F";
                document.getElementById('val-temp-a').innerText = data.temp_air < -100 ? "N/A" : data.temp_air.toFixed(1) + " °F";

                const modes = ["AUTO", "USER OVERRIDE", "SERVICE"];
                const waterModes = ["OFF", "POOL", "SPA"];
                document.getElementById('val-mode').innerText = modes[data.system_mode];
                document.getElementById('val-water').innerText = waterModes[data.water_mode];
                
                document.getElementById('btn-wm-0').className = (data.water_mode === 0) ? "action-btn active-red" : "action-btn";
                document.getElementById('btn-wm-1').className = (data.water_mode === 1) ? "action-btn active-actuator" : "action-btn";
                document.getElementById('btn-wm-2').className = (data.water_mode === 2) ? "action-btn active-actuator" : "action-btn";

                // Update target inputs only if the user isn't actively typing in them
                if (document.activeElement.id !== 'target-pool') document.getElementById('target-pool').value = data.target_pool_temp;
                if (document.activeElement.id !== 'target-spa') document.getElementById('target-spa').value = data.target_spa_temp;

                inServiceMode = (data.system_mode === 2);
                const srvBtn = document.getElementById('btn-service');
                const hwToggles = document.getElementById('hardware-toggles');
                
                if (inServiceMode) {
                    srvBtn.innerText = "Exit Service Mode";
                    srvBtn.className = "action-btn active-red";
                    hwToggles.style.opacity = "1"; hwToggles.style.pointerEvents = "auto";
                } else {
                    srvBtn.innerText = "Enable Service Mode";
                    srvBtn.className = "action-btn";
                    hwToggles.style.opacity = "0.5"; hwToggles.style.pointerEvents = "none";
                }

                updateRelayButton('btn-relay-0', data.relay_filter_pump);
                updateRelayButton('btn-relay-1', data.relay_aux_pump);
                updateRelayButton('btn-relay-2', data.relay_vacuum_pump);
                updateRelayButton('btn-relay-3', data.relay_spa_blower);
                updateRelayButton('btn-relay-4', data.relay_pool_lights);
                updateActuatorButton('btn-relay-5', data.actuator_intake_spa, "POOL", "SPA");
                updateActuatorButton('btn-relay-6', data.actuator_return_spa, "SPLIT", "SPA");
                updateRelayButton('btn-relay-7', data.relay_heater_igniter);

            } catch (err) { console.error("Status fetch failed", err); }
        }

        async function toggleServiceMode() {
            const targetMode = inServiceMode ? 0 : 2; 
            try {
                const res = await fetch('/api/mode', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ system_mode: targetMode }) });
                if (res.ok) {
                    showToast(inServiceMode ? "Returning to AUTO Mode..." : "Service Mode Active: Loop Halted.", false);
                    fetchStatus();
                } else {
                    const data = await res.json(); showToast(data.error, true);
                }
            } catch (err) { showToast("Network error contacting controller.", true); }
        }

        async function setRelay(relayIdx) {
            const btn = document.getElementById(`btn-relay-${relayIdx}`);
            const targetState = btn.dataset.targetState === "true";
            try {
                const res = await fetch('/api/service', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ relay: relayIdx, state: targetState }) });
                if (res.ok) { fetchStatus(); } 
                else { const data = await res.json(); showToast(data.error, true); }
            } catch (err) { showToast("Network error contacting controller.", true); }
        }
        
        async function setWaterMode(modeIdx) {
            try {
                const res = await fetch('/api/override', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ water_mode: modeIdx, timeout: 120 })
                });
                if (res.ok) {
                    showToast("Water Mode updated.");
                    fetchStatus();
                } else {
                    const data = await res.json(); 
                    showToast(data.error, true);
                }
            } catch (err) { showToast("Network error contacting controller.", true); }
        }

        async function saveTargetTemps() {
            const p = parseFloat(document.getElementById('target-pool').value);
            const s = parseFloat(document.getElementById('target-spa').value);
            try {
                const res = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ target_pool_temp: p, target_spa_temp: s }) });
                if (res.ok) showToast("Temperatures saved successfully.");
                else showToast("Failed to save temperatures.", true);
            } catch (err) { showToast("Network error.", true); }
        }

        // API Calls: Settings Tab
        function formatTime(h, m) { return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}`; }
        function parseTime(str) { const pts = str.split(':'); return { h: parseInt(pts[0]), m: parseInt(pts[1]) }; }

        async function loadSettings() {
            try {
                const res = await fetch('/api/settings');
                const data = await res.json();
                
                document.getElementById('cal-freeze').value = data.freeze_protect_temp;
                document.getElementById('cal-offset').value = data.water_temp_offset;

                document.getElementById('sched-filter-start').value = formatTime(data.sched_filter.startHour, data.sched_filter.startMin);
                document.getElementById('sched-filter-end').value = formatTime(data.sched_filter.endHour, data.sched_filter.endMin);
                document.getElementById('sched-filter-en').innerText = data.sched_filter.enabled ? "ON" : "OFF";
                document.getElementById('sched-filter-en').dataset.enabled = data.sched_filter.enabled;

                document.getElementById('sched-vac-start').value = formatTime(data.sched_vacuum.startHour, data.sched_vacuum.startMin);
                document.getElementById('sched-vac-end').value = formatTime(data.sched_vacuum.endHour, data.sched_vacuum.endMin);
                document.getElementById('sched-vac-en').innerText = data.sched_vacuum.enabled ? "ON" : "OFF";
                document.getElementById('sched-vac-en').dataset.enabled = data.sched_vacuum.enabled;

                document.getElementById('sched-lights-start').value = formatTime(data.sched_lights.startHour, data.sched_lights.startMin);
                document.getElementById('sched-lights-end').value = formatTime(data.sched_lights.endHour, data.sched_lights.endMin);
                document.getElementById('sched-lights-en').innerText = data.sched_lights.enabled ? "ON" : "OFF";
                document.getElementById('sched-lights-en').dataset.enabled = data.sched_lights.enabled;

            } catch (err) { showToast("Failed to load settings.", true); }
        }

        async function saveSettings() {
            const fStart = parseTime(document.getElementById('sched-filter-start').value);
            const fEnd = parseTime(document.getElementById('sched-filter-end').value);
            const vStart = parseTime(document.getElementById('sched-vac-start').value);
            const vEnd = parseTime(document.getElementById('sched-vac-end').value);
            const lStart = parseTime(document.getElementById('sched-lights-start').value);
            const lEnd = parseTime(document.getElementById('sched-lights-end').value);

            const payload = {
                freeze_protect_temp: parseFloat(document.getElementById('cal-freeze').value),
                water_temp_offset: parseFloat(document.getElementById('cal-offset').value),
                sched_filter: { startHour: fStart.h, startMin: fStart.m, endHour: fEnd.h, endMin: fEnd.m, enabled: document.getElementById('sched-filter-en').dataset.enabled === "true" },
                sched_vacuum: { startHour: vStart.h, startMin: vStart.m, endHour: vEnd.h, endMin: vEnd.m, enabled: document.getElementById('sched-vac-en').dataset.enabled === "true" },
                sched_lights: { startHour: lStart.h, startMin: lStart.m, endHour: lEnd.h, endMin: lEnd.m, enabled: document.getElementById('sched-lights-en').dataset.enabled === "true" }
            };

            try {
                const res = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
                if (res.ok) showToast("Settings saved to Flash memory.");
                else showToast("Failed to save settings.", true);
            } catch (err) { showToast("Network error.", true); }
        }

        // API Calls: Wi-Fi & System
        async function loadNetwork() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();
                document.getElementById('wifi-ssid').value = data.ssid || "";
                document.getElementById('mqtt-broker').value = data.mqtt_broker || "";
                document.getElementById('mqtt-port').value = data.mqtt_port || "1883";
            } catch (err) { console.error("Failed to load network settings"); }
        }

        async function saveWiFi() {
            const s = document.getElementById('wifi-ssid').value;
            const p = document.getElementById('wifi-pass').value;
            if(!s || !p) { showToast("Both SSID and Password are required", true); return; }
            try {
                const res = await fetch('/api/wifi', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ ssid: s, pass: p }) });
                if (res.ok) { showToast("Wi-Fi updated! Rebooting..."); setTimeout(() => window.location.reload(), 5000); }
                else showToast("Failed to update Wi-Fi.", true);
            } catch (err) { showToast("Network error.", true); }
        }

        async function saveMQTT() {
            const mBroker = document.getElementById('mqtt-broker').value;
            const mPort = parseInt(document.getElementById('mqtt-port').value) || 1883;
            try {
                const res = await fetch('/api/wifi', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ mqtt_broker: mBroker, mqtt_port: mPort }) });
                if (res.ok) { showToast("MQTT updated! Rebooting..."); setTimeout(() => window.location.reload(), 5000); }
                else showToast("Failed to update MQTT.", true);
            } catch (err) { showToast("Network error.", true); }
        }

        async function rebootSystem() {
            if(!confirm("Are you sure you want to reboot the pool controller?")) return;
            try {
                const res = await fetch('/api/reboot', { method: 'POST' });
                if (res.ok) { showToast("Rebooting..."); setTimeout(() => window.location.reload(), 5000); }
            } catch (err) { showToast("Reboot command sent."); }
        }

        // Init Dashboard
        fetchStatus();
        setInterval(fetchStatus, 2000);
    </script>
</body>
</html>
)rawliteral";

#endif