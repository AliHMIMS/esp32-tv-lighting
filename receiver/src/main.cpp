// TV ambient lighting, fed by a screen grabber app on the TV.
// hyperion_server.cpp receives frames over Wi-Fi, leds.cpp turns the screen
// edges into LED colours, web.cpp serves the setup page at
// http://tv-ambilight.local/.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "hyperion_server.h"
#include "leds.h"
#include "secrets.h"
#include "web.h"

#define HOSTNAME "tv-ambilight"

// OTA comes up before anything else so a later failure never stops us from
// reflashing over Wi-Fi.
static void start_ota() {
  ArduinoOTA.setHostname(HOSTNAME);  // also advertises HOSTNAME.local via mDNS
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    Serial.println("OTA update starting");
    leds::pause(true);
  });
  ArduinoOTA.onEnd([] { Serial.println("\nOTA update done, rebooting"); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    Serial.printf("OTA %u%%\r", done * 100 / total);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA error %u\n", e);
    leds::pause(false);
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nTV Ambilight receiver");

  leds::begin();  // blank the strip early, before Wi-Fi

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);  // modem sleep adds 100+ ms of jitter to incoming frames
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nConnected, IP %s\n", WiFi.localIP().toString().c_str());
  start_ota();

  hyperion::begin();
  MDNS.addService("hyperiond-flatbuf", "tcp", hyperion::PORT);
  web::begin();
  Serial.printf("Setup page: http://%s.local/  Grabber target: %s:%u\n", HOSTNAME,
                WiFi.localIP().toString().c_str(), hyperion::PORT);
}

void loop() {
  ArduinoOTA.handle();
  leds::loop();
  delay(10);
}
