#include "web.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "hyperion_server.h"
#include "leds.h"
#include "web_ui.h"

// Config fields that map 1:1 to JSON keys.
#define CONFIG_FIELDS(X)                                                                   \
  X(n_left) X(n_top) X(n_right) X(start_corner) X(depth) X(enabled) X(brightness)          \
  X(saturation) X(gamma) X(smoothing_ms) X(gain_r) X(gain_g) X(gain_b) X(max_milliamps) \
  X(timeout_s)

static esp_err_t send_json(httpd_req_t *req, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, out.c_str(), out.length());
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t config_get_handler(httpd_req_t *req) {
  Config c = leds::get_config();
  JsonDocument doc;
#define TO_JSON(f) doc[#f] = c.f;
  CONFIG_FIELDS(TO_JSON)
#undef TO_JSON
  return send_json(req, doc);
}

static bool read_body(httpd_req_t *req, char *body, size_t cap) {
  if (req->content_len >= cap) return false;
  size_t got = 0;
  while (got < req->content_len) {
    int r = httpd_req_recv(req, body + got, req->content_len - got);
    if (r <= 0) return false;
    got += r;
  }
  body[got] = 0;
  return true;
}

// Accepts a partial config; only the keys present are changed.
static esp_err_t config_post_handler(httpd_req_t *req) {
  char body[1024];
  JsonDocument doc;
  if (!read_body(req, body, sizeof(body)) || deserializeJson(doc, body)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
  }
  Config c = leds::get_config();
#define FROM_JSON(f) c.f = doc[#f] | c.f;
  CONFIG_FIELDS(FROM_JSON)
#undef FROM_JSON
  leds::set_config(c);
  return config_get_handler(req);
}

static esp_err_t status_handler(httpd_req_t *req) {
  Status s = leds::status();
  JsonDocument doc;
  doc["client"] = hyperion::client();
  doc["port"] = hyperion::PORT;
  doc["ip"] = WiFi.localIP().toString();
  doc["signal"] = s.signal;
  doc["fps"] = roundf(s.fps * 10) / 10;
  doc["width"] = s.width;
  doc["height"] = s.height;
  doc["frames"] = s.frames;
  if (s.ms_since_frame != UINT32_MAX) doc["ms_since_frame"] = s.ms_since_frame;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime_s"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  return send_json(req, doc);
}

// Raw RGB bytes in strip order, for the live preview.
static esp_err_t leds_handler(httpd_req_t *req) {
  static uint8_t rgb[MAX_LEDS * 3];
  size_t n = leds::get_leds(rgb, sizeof(rgb));
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, (const char *)rgb, n);
}

// Last received frame, downscaled, as raw RGB with its size in headers.
static esp_err_t frame_handler(httpd_req_t *req) {
  static uint8_t rgb[96 * 96 * 3];
  int w = 0, h = 0;
  size_t n = leds::get_preview(rgb, sizeof(rgb), w, h);
  char ws[8], hs[8];
  snprintf(ws, sizeof(ws), "%d", w);
  snprintf(hs, sizeof(hs), "%d", h);
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Width", ws);
  httpd_resp_set_hdr(req, "X-Height", hs);
  return httpd_resp_send(req, (const char *)rgb, n);
}

// POST /api/test {"pattern":"rainbow"|"corners"|"off"}
static esp_err_t test_handler(httpd_req_t *req) {
  char body[128];
  JsonDocument doc;
  if (!read_body(req, body, sizeof(body)) || deserializeJson(doc, body)) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
  }
  String p = doc["pattern"] | "off";
  if (p == "rainbow") leds::test_pattern(TestPattern::Rainbow, 15000);
  else if (p == "corners") leds::test_pattern(TestPattern::Corners, 20000);
  else leds::test_pattern(TestPattern::None, 0);
  return httpd_resp_sendstr(req, "ok");
}

void web::begin() {
  httpd_handle_t server = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  cfg.core_id = 0;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    Serial.println("HTTP server failed to start");
    return;
  }
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, index_handler, nullptr},
      {"/api/config", HTTP_GET, config_get_handler, nullptr},
      {"/api/config", HTTP_POST, config_post_handler, nullptr},
      {"/api/status", HTTP_GET, status_handler, nullptr},
      {"/api/leds", HTTP_GET, leds_handler, nullptr},
      {"/api/frame", HTTP_GET, frame_handler, nullptr},
      {"/api/test", HTTP_POST, test_handler, nullptr},
  };
  for (const auto &r : routes) httpd_register_uri_handler(server, &r);
}
