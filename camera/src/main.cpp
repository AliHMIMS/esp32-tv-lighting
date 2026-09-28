// Camera-based TV ambient lighting.
// The OV2640 watches the TV, ambilight.cpp turns the screen edges into LED
// colours, web.cpp serves the setup page at http://tv-ambilight.local/.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "ambilight.h"
#include "secrets.h"
#include "web.h"

#define HOSTNAME "tv-ambilight"

// OTA comes up before anything else so a later failure (camera, LEDs, ...)
// never stops us from reflashing over Wi-Fi.
static void start_ota() {
  ArduinoOTA.setHostname(HOSTNAME);  // also advertises HOSTNAME.local via mDNS
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    Serial.println("OTA update starting");
    ambilight::pause(true);
  });
  ArduinoOTA.onEnd([] { Serial.println("\nOTA update done, rebooting"); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    Serial.printf("OTA %u%%\r", done * 100 / total);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA error %u\n", e);
    ambilight::pause(false);
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nTV Ambilight");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nConnected, IP %s\n", WiFi.localIP().toString().c_str());
  start_ota();

  if (!ambilight::begin()) Serial.println("Ambilight failed to start (camera?)");
  web::begin();
  Serial.printf("Setup page: http://%s.local/\n", HOSTNAME);
}

void loop() {
  ArduinoOTA.handle();
  ambilight::loop();
  delay(10);
}
