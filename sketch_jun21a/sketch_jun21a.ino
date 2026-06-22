#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>

const char* ssid = "FREE-LAN-Tester";

ESP8266WebServer server(80);
DNSServer dnsServer;

// Pin assignments
const int lanPins[8] = {16, -1, 5, 4, 0, 2, 14, 12};
int pinStates[8] = {0, 0, 0, 0, 0, 0, 0, 0};

void readPins() {
  for (int i = 0; i < 8; i++) {
    if (lanPins[i] == -1) {
      pinStates[i] = 0; // Pin 2 is Ground loop reference
    } else {
      // Reads 1 if voltage is applied. 
      // NOTE: Ensure you use external 10k pull-down resistors on these pins to stop floating!
      pinStates[i] = digitalRead(lanPins[i]); 
    }
  }
}

void handleStatus() {
  readPins();
  String json = "{";
  for(int i = 0; i < 8; i++) {
    json += "\"p" + String(i+1) + "\":" + String(pinStates[i]);
    if(i < 7) json += ",";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = R"=====(
  <!DOCTYPE html>
  <html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Interactive 2-Pair LAN Tester</title>
    <style>
      body { font-family: 'Segoe UI', Arial, sans-serif; text-align: center; background: #1e1e24; color: #fff; padding: 10px; margin: 0; }
      .container { max-width: 500px; margin: 20px auto; background: #2a2a35; padding: 20px; border-radius: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.3); }
      h2 { margin-bottom: 5px; color: #4cc9f0; }
      .instructions { color: #aaa; font-size: 14px; margin-bottom: 20px; }
      
      /* Group layout by Pairs */
      .pair-container { border: 2px dashed #444; padding: 10px; margin-bottom: 15px; border-radius: 8px; background: rgba(255,255,255,0.02); }
      .pair-title { font-size: 12px; text-transform: uppercase; color: #ff007f; text-align: left; margin-bottom: 5px; font-weight: bold;}
      .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
      
      .pin-card { padding: 15px; border-radius: 8px; font-weight: bold; color: white; cursor: pointer; transition: transform 0.2s, border 0.2s; border: 2px solid transparent; }
      .pin-card:hover { transform: scale(1.02); }
      
      /* States */
      .active { background-color: #2ec4b6; box-shadow: 0 0 10px rgba(46, 196, 182, 0.4); }
      .inactive { background-color: #e71d36; opacity: 0.6; }
      
      /* Highlight focus state when clicked */
      .focused-pair { border: 2px solid #ff007f !important; animation: pulse 1.5s infinite; }
      @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.6; } 100% { opacity: 1; } }
    </style>
    <script>
      let activeFocusGroup = null;

      setInterval(() => {
        fetch('/status').then(r => r.json()).then(data => {
          updateCard('p1', data.p1, 'Pin 1: Tx +', 'group1');
          updateCard('p2', data.p2, 'Pin 2: Tx - (GND)', 'group1');
          updateCard('p3', data.p3, 'Pin 3: Rx +', 'group2');
          updateCard('p6', data.p6, 'Pin 6: Rx -', 'group2');
          updateCard('p4', data.p4, 'Pin 4: DataB +', 'group3');
          updateCard('p5', data.p5, 'Pin 5: DataB -', 'group3');
          updateCard('p7', data.p7, 'Pin 7: DataC +', 'group4');
          updateCard('p8', data.p8, 'Pin 8: DataC -', 'group4');
        });
      }, 400);

      function updateCard(id, state, label, groupClass) {
        const el = document.getElementById(id);
        if(!el) return;
        
        // Retain group metadata
        el.dataset.group = groupClass;

        // Force Pin 2 as active placeholder reference 
        if(id === 'p2') {
          el.className = `pin-card active ${activeFocusGroup === groupClass ? 'focused-pair' : ''}`;
          el.innerText = label + " [GND REF]";
          return;
        }

        let stateText = (state === 1) ? " [LIVE]" : " [NO SIGNAL]";
        el.className = `pin-card ${(state === 1) ? 'active' : 'inactive'} ${activeFocusGroup === groupClass ? 'focused-pair' : ''}`;
        el.innerText = label + stateText;
      }

      // Feature: Clicking a pin isolates and highlights its matching pair!
      function handlePinClick(el) {
        const targetGroup = el.dataset.group;
        if (activeFocusGroup === targetGroup) {
          activeFocusGroup = null; // Toggle off if clicked again
        } else {
          activeFocusGroup = targetGroup; // Focus on this 2-pair connection
        }
      }
    </script>
  </head>
  <body>
    <div class="container">
      <h2>Interactive 2-Pair LAN Tester</h2>
      <p class="instructions">Click on any pin to isolate and track its matching twisted pair!</p>
      
      <div class="pair-container">
        <div class="pair-title">Pair 1: Transmit (Tx)</div>
        <div class="grid">
          <div id="p1" class="pin-card inactive" onclick="handlePinClick(this)">Pin 1: Loading...</div>
          <div id="p2" class="pin-card inactive" onclick="handlePinClick(this)">Pin 2: Loading...</div>
        </div>
      </div>

      <div class="pair-container">
        <div class="pair-title">Pair 2: Receive (Rx)</div>
        <div class="grid">
          <div id="p3" class="pin-card inactive" onclick="handlePinClick(this)">Pin 3: Loading...</div>
          <div id="p6" class="pin-card inactive" onclick="handlePinClick(this)">Pin 6: Loading...</div>
        </div>
      </div>

      <div class="pair-container">
        <div class="pair-title">Pair 3: Gigabit Extension A</div>
        <div class="grid">
          <div id="p4" class="pin-card inactive" onclick="handlePinClick(this)">Pin 4: Loading...</div>
          <div id="p5" class="pin-card inactive" onclick="handlePinClick(this)">Pin 5: Loading...</div>
        </div>
      </div>

      <div class="pair-container">
        <div class="pair-title">Pair 4: Gigabit Extension B</div>
        <div class="grid">
          <div id="p7" class="pin-card inactive" onclick="handlePinClick(this)">Pin 7: Loading...</div>
          <div id="p8" class="pin-card inactive" onclick="handlePinClick(this)">Pin 8: Loading...</div>
        </div>
      </div>

    </div>
  </body>
  </html>
  )=====";
  server.send(200, "text/html", html);
}

void setup() {
  for (int i = 0; i < 8; i++) {
    if (lanPins[i] != -1) {
      pinMode(lanPins[i], INPUT); 
    }
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid);

  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://192.168.4.1/"), true);
    server.send(302, "text/plain", "");
  });
  
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}