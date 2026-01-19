#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ---------------------------
// LED MATRIX CONFIG
// ---------------------------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN 5

MD_Parola display = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

uint16_t scrollSpeed = 100;   // default
uint8_t displayBrightness = 5; // default brightness (0–15)
bool darkMode = false; // inverted display flag

// ---------------------------
// WIFI + CAPTIVE PORTAL CONFIG
// ---------------------------
const char* password = NULL;

// ---------------------------
// Message structure
// ---------------------------
struct Message {
  String text;                 // The message itself
  unsigned long long timestamp; // Browser-provided timestamp (ms since epoch)
  String mac;                  // MAC address of device submitting
};

DNSServer dnsServer;
const byte DNS_PORT = 53;

WebServer server(80);

// ---------------------------
// MESSAGE STORAGE
// ---------------------------
const int MAX_MESSAGES = 10;
String messages[MAX_MESSAGES];
int messageCount = 0;

int currentMessageIndex = 0;
bool displayBusy = false;

// Fallback message when no user messages exist
const char* fallbackMessage =
  "Can you figure out how to add your message here?";

// ---------------------------
// BUILD HTML PAGE
// ---------------------------
String buildPage() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Message Portal</title>
    <style>
      body {
        margin: 0; padding: 0;
        font-family: Arial, sans-serif;
        background: linear-gradient(135deg, #4e54c8, #8f94fb);
        min-height: 100vh;
        display: flex;
        justify-content: center;
        align-items: flex-start;
        padding-top: 40px;
      }
      .card {
        background: white;
        width: 90%; max-width: 360px;
        padding: 25px;
        border-radius: 14px;
        box-shadow: 0 8px 20px rgba(0,0,0,0.25);
        text-align: center;
      }
      input[type=text] {
        width: 90%;
        padding: 10px;
        margin-top: 10px;
        border-radius: 6px;
        border: 1px solid #aaa;
        font-size: 16px;
      }
      .btn {
        margin-top: 15px;
        padding: 12px 20px;
        background: #4e54c8;
        color: white;
        border-radius: 8px;
        text-decoration: none;
        font-size: 16px;
        border: none;
      }
      .msgbox {
        margin-top: 20px;
        text-align: left;
        background: #f3f3ff;
        padding: 10px;
        border-radius: 8px;
      }
      .msg {
        background: #fff;
        padding: 8px;
        margin: 6px 0;
        border-radius: 6px;
        border-left: 4px solid #4e54c8;
        word-wrap: break-word;
        display: flex;
        align-items: center;
      }
      .msg input[type=checkbox] {
        margin-right: 10px;
        display: none;
      }
      #controlBar {
        margin-top: 10px;
        display: flex;
        justify-content: space-between;
        align-items: center;
      }
      #deleteBtn {
        background: #c84e4e;
        opacity: 0.5;
        /* keep the element in the layout; control visibility via visibility
           so the button can be shown/hidden without removing its layout space */
        display: inline-block;
      }
    </style>
 
    <script>
      function toggleEdit() {
        const boxes = document.querySelectorAll('.msg input[type=checkbox]');
        const deleteBtn = document.getElementById('deleteBtn');
        const moreBtn = document.getElementById('moreBtn');
        const secretBtn = document.getElementById('secretBtn');

        // Use computed style to determine current visibility state robustly
        const editing = boxes.length > 0 && window.getComputedStyle(boxes[0]).display !== 'inline-block';
 
        boxes.forEach(b => b.style.display = editing ? 'inline-block' : 'none');
 
        deleteBtn.style.visibility = editing ? 'visible' : 'hidden';
        secretBtn.style.display = editing ? 'block' : 'none';
 
        moreBtn.innerText = editing ? "Cancel" : "...";
      }
 
      function updateDeleteButton() {
        const checks = document.querySelectorAll('.msg input[type=checkbox]:checked');
        const btn = document.getElementById('deleteBtn');
        btn.style.opacity = checks.length > 0 ? "1" : "0.5";
        btn.disabled = checks.length === 0;
      }
 
      function submitDelete() {
        const checks = document.querySelectorAll('.msg input[type=checkbox]:checked');
        let form = document.getElementById('deleteForm');
        form.innerHTML = "";
        checks.forEach(c => {
          let hidden = document.createElement('input');
          hidden.type = 'hidden';
          hidden.name = 'del';
          hidden.value = c.value;
          form.appendChild(hidden);
        });
        form.submit();
      }
    </script>
    <script>
      document.addEventListener("DOMContentLoaded", () => {
      const tsField = document.getElementById("ts");
      tsField.value = Date.now();   // milliseconds since epoch
      });
  </script>

  <script>
    let secretPressCount = 0;
    let secretTimeout = null;

    function registerSecretPress() {
      secretPressCount++;

      // Reset if too slow
      if (secretTimeout) clearTimeout(secretTimeout);
      secretTimeout = setTimeout(() => {
        secretPressCount = 0;
      }, 1500);

      // Trigger after 5 presses
      if (secretPressCount >= 5) {
        secretPressCount = 0;
        document.getElementById("advancedPanel").style.display = "block";
      }
    }

    document.addEventListener("DOMContentLoaded", () => {
      const secretBtn = document.getElementById("secretBtn");
      if (secretBtn) {
        secretBtn.addEventListener("click", registerSecretPress);
      }
    });
  </script>

  <script>
  document.addEventListener("DOMContentLoaded", () => {
    const slider = document.getElementById("speedSlider");
    if (slider) {
      // Send new speed to ESP32 when slider moves
      slider.addEventListener("input", () => {
        fetch("/setSpeed", {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: "speed=" + slider.value
        });
      });
    }
  });

  document.addEventListener("DOMContentLoaded", () => {
    const brightSlider = document.getElementById("brightnessSlider");
    if (brightSlider) {
      brightSlider.addEventListener("input", () => {
        fetch("/setBrightness", {
          method: "POST",
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: "brightness=" + brightSlider.value
        });
      });
    }
  });

  </script>

  </head>
  <body>
    <div class="card">
      <h2>Send a Message</h2>
      <form action="/submit" method="POST">
        <input type="text" name="msg" placeholder="Enter message" required>
        <input type="hidden" id="ts" name="ts">
        <br>
        <button class="btn" type="submit">Send Message</button>
      </form>

      <div class="msgbox">
        <h3>Last Messages</h3>
  )rawliteral";

  for (int i = messageCount - 1; i >= 0; i--) {
    html += "<div class='msg'><input type='checkbox' value='" + String(i) +
            "' onclick='updateDeleteButton()'>" + messages[i] + "</div>";
  }

  // add the delete form used by submitDelete()
  html += "<form id='deleteForm' action='/delete' method='POST'></form>";

  html += R"rawliteral(


        <div id="controlBar" style="
          display:flex;
          align-items:center;
          justify-content:space-between;
        ">

          <!-- Left: More button -->
          <button id="moreBtn" class="btn"
                  style="margin:0; padding:6px 10px; font-size:14px; background:#666;"
                  onclick="toggleEdit()">...</button>

          <button id="secretBtn" style="
            display:none;
            width:40px;
            height:40px;
            background:transparent;
            border:none;
            opacity:0;
            cursor:pointer;
          "></button>

          <!-- Right: Delete button (hidden but keeps its space) -->
          <button id="deleteBtn" class="btn" disabled
                  style="visibility:hidden;"
                  onclick="submitDelete()">Delete</button>

        </div>

      <div id="advancedPanel" style="display: none;margin-top: 20px;padding: 15px;background: #eef;border-radius: 10px;">
        <h3>Advanced Settings</h3>

        <label for="speedSlider">Scroll Delay</label><br>
        <input type='range' id='speedSlider' min='20' max='200' value='" + String(scrollSpeed) + "' style='width:100%; margin-bottom:12px; display:block;'>

        <label for="brightnessSlider">Brightness</label><br>
        <input type='range' id='brightnessSlider' min='0' max='15' value='" + String(displayBrightness) + "' style='width:100%; margin-top:10px; margin-bottom:8px; display:block;'>

        <label for="darkModeToggle" style="display:flex; align-items:center; gap:10px; margin-top:6px;">
          <input type="checkbox" id="darkModeToggle" />
          Dark mode (invert display)
        </label>

        <button id="closeAdvanced" class="btn"
                style="background:#c84e4e; margin-top:20px;display:block;margin-left:auto;margin-right:auto;"
                onclick="document.getElementById('advancedPanel').style.display='none'">
          Close
        </button>

      </div>

    </div>
  </body>
  </html>
  )rawliteral";

  // post-build script: initialize dark toggle and wire it
  html += R"rawliteral(
   <script>
     document.addEventListener("DOMContentLoaded", () => {
       const darkToggle = document.getElementById("darkModeToggle");
       if (darkToggle) {
         // initialize checkbox after page builds
         darkToggle.checked = )rawliteral";
  html += String(darkMode ? "true" : "false");
  html += R"rawliteral(;
         darkToggle.addEventListener("change", () => {
           fetch("/setDarkMode", {
             method: "POST",
             headers: { "Content-Type": "application/x-www-form-urlencoded" },
             body: "dark=" + (darkToggle.checked ? "1" : "0")
           });
         });
       }
     });
   </script>
   )rawliteral";

  return html;
}

// ---------------------------
// CAPTIVE PORTAL REDIRECT
// ---------------------------
bool captivePortal() {
  if (!server.hostHeader().equals(WiFi.softAPIP().toString())) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
    return true;
  }
  return false;
}

// ---------------------------
// LOAD/SAVE MESSAGES
// ---------------------------
void loadMessages() {
  if (!SPIFFS.exists("/messages.txt")) return;

  File file = SPIFFS.open("/messages.txt", "r");
  if (!file) return;

  StaticJsonDocument<512> doc;
  if (!deserializeJson(doc, file)) {
    messageCount = doc["count"];
    for (int i = 0; i < messageCount; i++) {
      messages[i] = doc["messages"][i].as<String>();
    }
  }
  file.close();
}

void saveMessages() {
  StaticJsonDocument<512> doc;
  doc["count"] = messageCount;

  JsonArray arr = doc.createNestedArray("messages");
  for (int i = 0; i < messageCount; i++) {
    arr.add(messages[i]);
  }

  File file = SPIFFS.open("/messages.txt", "w");
  serializeJson(doc, file);
  file.close();
}

// ---------------------------
// Advanced Settings
// ---------------------------
void loadSettings() {
  if (!SPIFFS.exists("/settings.json")) return;

  File f = SPIFFS.open("/settings.json", "r");
  if (!f) return;

  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, f)) {
    scrollSpeed = doc["scrollSpeed"] | 100;
    displayBrightness = doc["brightness"] | 5;
    darkMode = doc["darkMode"] | false;
  }
  f.close();
}

void saveSettings() {
  StaticJsonDocument<128> doc;
  doc["scrollSpeed"] = scrollSpeed;
  doc["brightness"] = displayBrightness;
  doc["darkMode"] = darkMode;

  File f = SPIFFS.open("/settings.json", "w");
  serializeJson(doc, f);
  f.close();
}

// ---------------------------
// WEB HANDLERS
// ---------------------------
void handleSubmit() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String msg = server.arg("msg");
  msg.trim();
  if (msg.length() == 0) {
    server.send(400, "text/plain", "Empty message");
    return;
  }
  
  String ts  = server.arg("ts");   // timestamp from browser
  unsigned long long timestamp = ts.toInt();

  if (messageCount < MAX_MESSAGES) {
    messages[messageCount++] = msg;
  } else {
    for (int i = 1; i < MAX_MESSAGES; i++) {
      messages[i - 1] = messages[i];
    }
    messages[MAX_MESSAGES - 1] = msg;
  }

  Message m;
  m.text = msg;
  m.timestamp = timestamp;
  m.mac = WiFi.softAPmacAddress();

  saveMessages();
  server.send(200, "text/html", buildPage());
}

void handleDelete() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  bool toDelete[MAX_MESSAGES] = { false };

  for (int i = 0; i < server.args(); i++) {
    if (server.argName(i) == "del") {
      int idx = server.arg(i).toInt();
      if (idx >= 0 && idx < messageCount) {
        toDelete[idx] = true;
      }
    }
  }

  String newList[MAX_MESSAGES];
  int newCount = 0;

  for (int i = 0; i < messageCount; i++) {
    if (!toDelete[i]) {
      newList[newCount++] = messages[i];
    }
  }

  messageCount = newCount;
  for (int i = 0; i < newCount; i++) {
    messages[i] = newList[i];
  }

  saveMessages();
  server.send(200, "text/html", buildPage());
}

void handleSetSpeed() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  int s = server.arg("speed").toInt();
  if (s < 20) s = 20;
  if (s > 200) s = 200;

  scrollSpeed = s;

  // Apply speed immediately to the active Parola animation
  display.setSpeed(scrollSpeed);

  saveSettings();

  server.send(200, "text/plain", "OK");
}

void handleSetBrightness() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  int b = server.arg("brightness").toInt();
  if (b < 0) b = 0;
  if (b > 15) b = 15;

  displayBrightness = b;
  display.setIntensity(displayBrightness);
  saveSettings();

  server.send(200, "text/plain", "OK");
}

void handleSetDarkMode() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  int d = server.arg("dark").toInt(); // expect 0 or 1
  darkMode = (d != 0);
  display.setInvert(darkMode); // apply immediately
  saveSettings();

  server.send(200, "text/plain", "OK");
}

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleNotFound() {
  if (captivePortal()) return;
  server.send(404, "text/plain", "Not found");
}




// ---------------------------
// DISPLAY LOGIC
// ---------------------------
String getCurrentMessage() {
  if (messageCount == 0) return fallbackMessage;
  return messages[currentMessageIndex];
}

void startScrollingMessage() {
  static char scrollBuffer[200];   // safe, fixed memory

  String msg = getCurrentMessage();
  msg.toCharArray(scrollBuffer, sizeof(scrollBuffer));

  display.displayText(
    scrollBuffer,
    PA_LEFT,
    scrollSpeed,
    0,
    PA_SCROLL_LEFT,
    PA_SCROLL_LEFT
  );
}

void advanceMessageIndex() {
  if (messageCount == 0) return;
  currentMessageIndex++;
  if (currentMessageIndex >= messageCount) currentMessageIndex = 0;
}

// ---------------------------
// SETUP
// ---------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  SPIFFS.begin(true);
  loadSettings();
  loadMessages();

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  static char ssid[32];
  snprintf(ssid, sizeof(ssid), "MessageMe-%02X%02X%02X",mac[3],mac[4],mac[5]);
    
  WiFi.softAP(ssid, password);
  IPAddress apIP = WiFi.softAPIP();

  Serial.print("AP SSID: ");
  Serial.println(ssid);  
    
  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/", handleRoot);
  server.on("/submit", handleSubmit);
  server.on("/delete", handleDelete);
  server.on("/setSpeed", handleSetSpeed);
  server.on("/setBrightness", handleSetBrightness); // <-- add route
  server.on("/setDarkMode", handleSetDarkMode);
  server.onNotFound(handleNotFound);
  
  server.begin();

  display.begin();
  display.setIntensity(displayBrightness);
  display.setInvert(darkMode);
  display.displayClear();

  startScrollingMessage();
}

// ---------------------------
// LOOP
// ---------------------------
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (display.displayAnimate()) {
    displayBusy = false;
    advanceMessageIndex();
    startScrollingMessage();
  }
}
