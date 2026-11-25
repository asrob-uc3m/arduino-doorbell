/**
 * File: ArduinoDoorbell.ino
 * Version: 1.4.0
 * Description: Serves a web page to trigger a buzzer during configured weekly meeting times.
 *              Can run as an Access Point (AP) or connect to an existing network.
 *              Uses a 3-wire DS1302 RTC module and displays current RTC time in Serial and web UI.
 * Hardware: Arduino Nano 33 IoT, DS1302 RTC Module (CLK, DAT, RST), Piezo Buzzer
 * Required Libraries: WiFiNINA, Rtc by Makuna (ThreeWire.h, RtcDS1302.h)
 */

#include <SPI.h>
#include <WiFiNINA.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

// --- Configuration ---
// Set to 'true' to create an Access Point, or 'false' to connect to an existing Wi-Fi network
const bool USE_AP_MODE = true;

char ssid[] = "YOUR_WIFI_SSID";      // Network SSID (name of the AP to create or connect to)
char pass[] = "YOUR_WIFI_PASSWORD";  // Network password (at least 8 characters for AP mode)
int status = WL_IDLE_STATUS;

// --- Pin Configuration ---
const int BUZZER_PIN = 8;            // Pin connected to the Piezo Buzzer
const int RTC_DAT_PIN = 4;           // DS1302 DAT / IO pin
const int RTC_CLK_PIN = 5;           // DS1302 CLK / SCLK pin
const int RTC_RST_PIN = 2;           // DS1302 RST / CE pin

// --- Time Slot Configuration ---
struct TimeSlot {
  uint8_t dayOfWeek; // 0=Sunday, 1=Monday, 2=Tuesday, 3=Wednesday, 4=Thursday, 5=Friday, 6=Saturday
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
};

// You can increase this number and add more slots to the array below
const int NUM_SLOTS = 2;

TimeSlot meetingSlots[NUM_SLOTS] = {
  {3, 16, 0, 21, 0},   // Wednesday: 16:00 to 21:00
  {5, 11, 0, 14, 0}    // Friday: 11:00 to 14:00
};

const char* DAYS_OF_WEEK[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// --- Objects ---
WiFiServer server(80);
ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN); // (IO/DAT, SCLK/CLK, CE/RST)
RtcDS1302<ThreeWire> Rtc(myWire);

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 5000); // Wait up to 5 seconds for Serial

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize DS1302 RTC
  Serial.println("Initializing DS1302 RTC...");
  Rtc.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (Rtc.GetIsWriteProtected()) {
    Serial.println("RTC was write protected, enabling writing now");
    Rtc.SetIsWriteProtected(false);
  }

  if (!Rtc.GetIsRunning()) {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (!Rtc.IsDateTimeValid() || now < compiled) {
    Serial.println("RTC lost confidence in the DateTime or is older than compile time! Updating...");
    Rtc.SetDateTime(compiled);
    now = Rtc.GetDateTime();
  }

  Serial.println("DS1302 RTC initialized.");
  Serial.print("Current RTC Time: ");
  Serial.println(formatDateTime(now));

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
              RtcDateTime now = Rtc.GetDateTime();
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
    client.flush();
    delay(50);
    client.stop();
    Serial.println("Client disconnected\n");
  }
}

String formatDateTime(const RtcDateTime& dt) {
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%s (%u) %04u-%02u-%02u %02u:%02u:%02u",
           DAYS_OF_WEEK[dt.DayOfWeek()],
           dt.DayOfWeek(),
           dt.Year(),
           dt.Month(),
           dt.Day(),
           dt.Hour(),
           dt.Minute(),
           dt.Second());
  return String(buffer);
}

bool isMeetingTime(const RtcDateTime& now) {
  int currentDay = now.DayOfWeek();
  int currentMins = now.Hour() * 60 + now.Minute();

  for (int i = 0; i < NUM_SLOTS; i++) {
    // Only check times if the day of the week matches
    if (currentDay == meetingSlots[i].dayOfWeek) {
      int startMins = meetingSlots[i].startHour * 60 + meetingSlots[i].startMinute;
      int endMins = meetingSlots[i].endHour * 60 + meetingSlots[i].endMinute;

      if (currentMins >= startMins && currentMins < endMins) {
        return true;
      }
    }
  }
  return false;
}

void serveHTML(WiFiClient& client) {
  RtcDateTime now = Rtc.GetDateTime();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();

  // HTML & GUI
  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<title>Meeting Room Controls</title>");
  client.println("<style>");
  client.println("body { font-family: Georgia, serif; text-align: center; margin: 0; padding: 30px 15px; background-color: #1b4d96; color: #111; }");
  client.println(".card { background-color: #ffffff; max-width: 520px; margin: 22px auto; padding: 28px 24px; border-radius: 16px; box-shadow: 0 6px 18px rgba(0,0,0,0.2); }");
  client.println("h1, h2 { color: #d96b38; font-weight: normal; margin-top: 0; margin-bottom: 18px; }");
  client.println(".subtitle { font-size: 16px; margin-bottom: 22px; }");
  client.println(".slot { font-size: 16px; font-weight: bold; margin: 8px 0; }");
  client.println("button { font-family: sans-serif; padding: 16px 36px; font-size: 18px; font-weight: bold; cursor: pointer; background-color: #d96b38; color: white; border: none; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.18); }");
  client.println("button:active { background-color: #b85527; transform: translateY(2px); box-shadow: none; }");
  client.println("#status { margin-top: 20px; font-size: 16px; color: #111; }");
  client.println("#rtc-time { margin-top: 12px; font-size: 13px; color: #666; }");
  client.println("</style></head>");

  client.println("<body>");

  // Card 1: Schedule
  client.println("<div class=\"card\">");
  client.println("<h1>Horarios de reuniones</h1>");
  client.println("<div class=\"subtitle\">Solo esta habilitado el timbre en los siguientes horarios:</div>");
  for (int i = 0; i < NUM_SLOTS; i++) {
    char slotBuf[48];
    snprintf(slotBuf, sizeof(slotBuf), "%s: %02u:%02u - %02u:%02u",
             DAYS_OF_WEEK[meetingSlots[i].dayOfWeek],
             meetingSlots[i].startHour, meetingSlots[i].startMinute,
             meetingSlots[i].endHour, meetingSlots[i].endMinute);
    client.print("<div class=\"slot\">");
    client.print(slotBuf);
    client.println("</div>");
  }
  client.println("</div>");

  // Card 2: Doorbell Controls
  client.println("<div class=\"card\">");
  client.println("<h2>Timbre</h2>");
  client.println("<button onclick=\"triggerBuzzer()\">Sound Buzzer</button>");
  client.println("<div id=\"status\">Ready</div>");
  client.print("<div id=\"rtc-time\">Arduino Time: ");
  client.print(formatDateTime(now));
  client.println("</div>");
  client.println("</div>");

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
