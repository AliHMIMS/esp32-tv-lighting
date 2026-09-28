#include "web.h"

#include <ArduinoJson.h>
#include <esp_http_server.h>

#include "ambilight.h"
#include "web_ui.h"

// Config fields that map 1:1 to JSON keys (corners handled separately).
#define CONFIG_FIELDS(X)                                                                  \
  X(n_left) X(n_top) X(n_right) X(start_corner) X(depth) X(inset) X(enabled) X(brightness) \
  X(saturation) X(gamma) X(smoothing) X(black_level) X(gain_r) X(gain_g) X(gain_b)        \
  X(max_milliamps) X(aec) X(agc) X(awb) X(exposure) X(gain) X(vflip) X(hmirror)

static esp_err_t send_json(httpd_req_t *req, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, out.c_str(), out.length());
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req) {
  uint8_t *buf;
  size_t len;
  if (!ambilight::capture_jpeg(&buf, &len)) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char *)buf, len);
  free(buf);
  return res;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
  Config c = ambilight::get_config();
  JsonDocument doc;
  JsonArray corners = doc["corners"].to<JsonArray>();
  for (float v : c.corners) corners.add(v);
#define TO_JSON(f) doc[#f] = c.f;
  CONFIG_FIELDS(TO_JSON)
#undef TO_JSON
  return send_json(req, doc);
}

// Accepts a partial config; only the keys present are changed.
static esp_err_t config_post_handler(httpd_req_t *req) {
  if (req->content_len > 2048) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too big");
  char body[2049];
  int got = 0;
  while (got < (int)req->content_len) {
    int r = httpd_req_recv(req, body + got, req->content_len - got);
    if (r <= 0) return ESP_FAIL;
    got += r;
  }
  body[got] = 0;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");

  Config c = ambilight::get_config();
  JsonArray corners = doc["corners"];
  if (corners && corners.size() == 8) {
    for (int i = 0; i < 8; i++) c.corners[i] = corners[i] | c.corners[i];
  }
#define FROM_JSON(f) c.f = doc[#f] | c.f;
  CONFIG_FIELDS(FROM_JSON)
#undef FROM_JSON
  ambilight::set_config(c);
  return config_get_handler(req);
}

// Raw RGB bytes in strip order, for the live preview.
static esp_err_t leds_handler(httpd_req_t *req) {
  static uint8_t rgb[MAX_LEDS * 3];
  size_t n = ambilight::get_leds(rgb, sizeof(rgb));
  char fps[16];
  snprintf(fps, sizeof(fps), "%.1f", ambilight::fps());
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "X-FPS", fps);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, (const char *)rgb, n);
}

void web::begin() {
  httpd_handle_t server = nullptr;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    Serial.println("HTTP server failed to start");
    return;
  }
  const httpd_uri_t routes[] = {
      {"/", HTTP_GET, index_handler, nullptr},
      {"/capture", HTTP_GET, capture_handler, nullptr},
      {"/api/config", HTTP_GET, config_get_handler, nullptr},
      {"/api/config", HTTP_POST, config_post_handler, nullptr},
      {"/api/leds", HTTP_GET, leds_handler, nullptr},
  };
  for (const auto &r : routes) httpd_register_uri_handler(server, &r);
}
