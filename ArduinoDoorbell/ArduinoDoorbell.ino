/**
 * File: ArduinoDoorbell.ino
 * Version: 1.1.0
 * Description: Serves a web page to trigger a buzzer during configured meeting times.
 *              Can run as an Access Point (AP) or connect to an existing network.
 * Hardware: Arduino Nano 33 IoT, I2C RTC Module (DS3231/DS1307), Piezo Buzzer
 * Required Libraries: WiFiNINA, RTClib
 */

#include <SPI.h>
#include <WiFiNINA.h>
#include <Wire.h>
#include <RTClib.h>

// --- Configuration ---
// Set to 'true' to create an Access Point, or 'false' to connect to an existing Wi-Fi network
const bool USE_AP_MODE = true; 

char ssid[] = "YOUR_WIFI_SSID";      // Network SSID (name of the AP to create or connect to)
char pass[] = "YOUR_WIFI_PASSWORD";  // Network password (at least 8 characters for AP mode)
int status = WL_IDLE_STATUS;

const int BUZZER_PIN = 8;            // Pin connected to the Piezo Buzzer

// --- Time Slot Configuration ---
struct TimeSlot {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
};

// You can increase this number and add more slots to the array below
const int NUM_SLOTS = 2; 

TimeSlot meetingSlots[NUM_SLOTS] = {
  {9, 0, 10, 30},   // Slot 1: 09:00 to 10:30
  {14, 0, 15, 30}   // Slot 2: 14:00 to 15:30
};

// --- Objects ---
WiFiServer server(80);
RTC_DS3231 rtc; // Change to RTC_DS1307 if your MH-RTC module uses the older chip

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 5000); // Wait up to 5 seconds for Serial

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC module!");
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time to compile time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Check for WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  // Initialize Network Connection
  if (USE_AP_MODE) {
    Serial.print("Creating Access Point named: ");
    Serial.println(ssid);
    
    status = WiFi.beginAP(ssid, pass);
    if (status != WL_AP_LISTENING) {
      Serial.println("Creating access point failed");
      while (true); // halt
    }
    delay(10000); // Wait 10 seconds for the AP to fully initialize
  } else {
    // Attempt to connect to existing WiFi network
    while (status != WL_CONNECTED) {
      Serial.print("Attempting to connect to Network named: ");
      Serial.println(ssid);
      status = WiFi.begin(ssid, pass);
      delay(10000);
    }
  }

  server.begin();
  
  if (USE_AP_MODE) {
    Serial.println("Access Point created successfully.");
  } else {
    Serial.println("WiFi connected.");
  }
  
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Web server started. Waiting for clients...");
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("New client connected");
    String currentLine = "";
    bool isGetRequest = false;
    String requestPath = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        
        if (c == '\n') {
          // If the current line is blank, we got two newline characters in a row.
          // That's the end of the client HTTP request.
          if (currentLine.length() == 0) {
            
            // Handle the API endpoint for the buzzer
            if (requestPath.startsWith("GET /buzz")) {
              DateTime now = rtc.now();
              if (isMeetingTime(now)) {
                tone(BUZZER_PIN, 1000, 1000); // 1000 Hz tone for 1000 ms
                
                client.println("HTTP/1.1 200 OK");
                client.println("Content-type:text/plain");
                client.println("Connection: close");
                client.println();
                client.println("Buzzer sounded successfully!");
                Serial.println("Action: Buzzer SOUNDED (Valid meeting time)");
              } else {
                client.println("HTTP/1.1 403 Forbidden");
                client.println("Content-type:text/plain");
                client.println("Connection: close");
                client.println();
                client.println("Ignored: Outside of configured meeting times.");
                Serial.println("Action: Buzzer BLOCKED (Outside meeting times)");
              }
            } 
            // Serve the main HTML GUI
            else {
              serveHTML(client);
            }
            break;
          } else {
            // Parse the first line to get the request path
            if (currentLine.startsWith("GET ")) {
              requestPath = currentLine;
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected\n");
  }
}

bool isMeetingTime(DateTime now) {
  int currentMins = now.hour() * 60 + now.minute();
  
  for (int i = 0; i < NUM_SLOTS; i++) {
    int startMins = meetingSlots[i].startHour * 60 + meetingSlots[i].startMinute;
    int endMins = meetingSlots[i].endHour * 60 + meetingSlots[i].endMinute;
    
    if (currentMins >= startMins && currentMins < endMins) {
      return true;
    }
  }
  return false;
}

void serveHTML(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  
  // HTML & GUI
  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<title>Meeting Buzzer</title>");
  client.println("<style>");
  client.println("body { font-family: sans-serif; text-align: center; margin-top: 50px; background-color: #f4f4f9; }");
  client.println("button { padding: 20px 40px; font-size: 24px; cursor: pointer; background-color: #007bff; color: white; border: none; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }");
  client.println("button:active { background-color: #0056b3; transform: translateY(2px); box-shadow: none; }");
  client.println("#status { margin-top: 20px; font-size: 18px; font-weight: bold; color: #333; }");
  client.println("</style></head>");
  
  client.println("<body>");
  client.println("<h1>Meeting Room Controls</h1>");
  client.println("<button onclick=\"triggerBuzzer()\">Sound Buzzer</button>");
  client.println("<div id=\"status\">Ready</div>");
  
  // JavaScript to handle the button press without reloading the page
  client.println("<script>");
  client.println("function triggerBuzzer() {");
  client.println("  var statusDiv = document.getElementById('status');");
  client.println("  statusDiv.innerHTML = 'Sending request...';");
  client.println("  statusDiv.style.color = '#333';");
  client.println("  fetch('/buzz')");
  client.println("    .then(response => {");
  client.println("      if(response.ok) { statusDiv.style.color = 'green'; }");
  client.println("      else { statusDiv.style.color = 'red'; }");
  client.println("      return response.text();");
  client.println("    })");
  client.println("    .then(text => { statusDiv.innerHTML = text; })");
  client.println("    .catch(err => { statusDiv.innerHTML = 'Network error'; statusDiv.style.color = 'red'; });");
  client.println("}");
  client.println("</script>");
  
  client.println("</body></html>");
  client.println();
}