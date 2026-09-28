#include "ambilight.h"

#include <FastLED.h>
#include <Preferences.h>
#include <esp_camera.h>
#include <img_converters.h>

#include "camera_pins.h"

#define LED_PIN 14

static constexpr uint32_t CONFIG_VERSION = 1;
static constexpr int GRID = 4;  // GRID x GRID sample points per LED
static constexpr int SAMPLES = GRID * GRID;

static Config cfg;
static portMUX_TYPE cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool cfg_dirty = true;
static volatile uint32_t save_due = 0;  // millis() when to persist, 0 = nothing pending
static Preferences prefs;

static CRGB leds[MAX_LEDS];
static CLEDController *strip = nullptr;
static float smooth[MAX_LEDS][3];
static uint32_t sample_idx[MAX_LEDS][SAMPLES];  // pixel index into the frame
static volatile int n_leds = 0;
static uint8_t gamma_lut[256];

static volatile float cur_fps = 0;
static volatile bool paused = false;

static SemaphoreHandle_t jpeg_lock, jpeg_done;
static volatile bool jpeg_requested = false;
static uint8_t *jpeg_buf = nullptr;
static size_t jpeg_len = 0;

// ---------------------------------------------------------------- config

static Config default_config() {
  Config c = {};
  c.version = CONFIG_VERSION;
  // Rough corners from the first coffee-table frame; refined in the web UI.
  const float corners[8] = {0.347f, 0.256f, 0.734f, 0.267f, 0.734f, 0.558f, 0.328f, 0.538f};
  memcpy(c.corners, corners, sizeof(corners));
  c.n_left = 41;
  c.n_top = 73;
  c.n_right = 41;
  c.start_corner = 0;
  c.depth = 0.12f;
  c.inset = 0.01f;
  c.enabled = true;
  c.brightness = 160;
  c.saturation = 1.4f;
  c.gamma = 2.0f;
  c.smoothing = 0.6f;
  c.black_level = 16;
  c.gain_r = 1.0f;
  c.gain_g = 0.9f;
  c.gain_b = 0.8f;
  c.max_milliamps = 8000;  // 10A supply, leave headroom for the ESP32
  c.aec = false;
  c.agc = false;
  c.awb = true;
  c.exposure = 120;
  c.gain = 0;
  c.vflip = false;
  c.hmirror = false;
  return c;
}

static void sanitize(Config &c) {
  c.version = CONFIG_VERSION;
  for (float &v : c.corners) v = constrain(v, 0.0f, 1.0f);
  c.n_left = min<int>(c.n_left, MAX_LEDS);
  c.n_top = min<int>(c.n_top, MAX_LEDS - c.n_left);
  c.n_right = min<int>(c.n_right, MAX_LEDS - c.n_left - c.n_top);
  c.start_corner = c.start_corner ? 1 : 0;
  c.depth = constrain(c.depth, 0.01f, 0.45f);
  c.inset = constrain(c.inset, 0.0f, 0.2f);
  c.saturation = constrain(c.saturation, 0.0f, 4.0f);
  c.gamma = constrain(c.gamma, 0.5f, 4.0f);
  c.smoothing = constrain(c.smoothing, 0.0f, 0.98f);
  c.gain_r = constrain(c.gain_r, 0.0f, 2.0f);
  c.gain_g = constrain(c.gain_g, 0.0f, 2.0f);
  c.gain_b = constrain(c.gain_b, 0.0f, 2.0f);
  c.max_milliamps = constrain(c.max_milliamps, 200, 9500);
  c.exposure = min<int>(c.exposure, 1200);
  c.gain = min<int>(c.gain, 30);
}

Config ambilight::get_config() {
  portENTER_CRITICAL(&cfg_mux);
  Config c = cfg;
  portEXIT_CRITICAL(&cfg_mux);
  return c;
}

void ambilight::set_config(const Config &in) {
  Config c = in;
  sanitize(c);
  portENTER_CRITICAL(&cfg_mux);
  cfg = c;
  portEXIT_CRITICAL(&cfg_mux);
  cfg_dirty = true;
  save_due = millis() + 3000;  // sliders fire often; don't wear out flash
}

void ambilight::loop() {
  if (save_due && (int32_t)(millis() - save_due) >= 0) {
    save_due = 0;
    Config c = ambilight::get_config();
    prefs.putBytes("cfg", &c, sizeof(c));
    Serial.println("Config saved");
  }
}

// ---------------------------------------------------------------- camera

static bool init_camera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;  // raw pixels: no JPEG decode per frame
  c.frame_size = FRAMESIZE_QVGA;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

static void apply_sensor(const Config &c) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_exposure_ctrl(s, c.aec);
  s->set_aec2(s, 0);
  if (!c.aec) s->set_aec_value(s, c.exposure);
  s->set_gain_ctrl(s, c.agc);
  if (!c.agc) s->set_agc_gain(s, c.gain);
  s->set_whitebal(s, c.awb);
  s->set_awb_gain(s, c.awb);
  s->set_vflip(s, c.vflip);
  s->set_hmirror(s, c.hmirror);
}

// ---------------------------------------------------------------- sampling map

// Projective map from the unit square (screen coords u,v) to the TV quad in
// the image (Heckbert's square-to-quad). Handles any camera angle.
struct Homography {
  float a, b, c, d, e, f, g, h;
  void map(float u, float v, float &x, float &y) const {
    float w = g * u + h * v + 1.0f;
    x = (a * u + b * v + c) / w;
    y = (d * u + e * v + f) / w;
  }
};

static Homography square_to_quad(const float *p) {
  float x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3];
  float x2 = p[4], y2 = p[5], x3 = p[6], y3 = p[7];
  float dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
  float dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
  Homography m;
  float det = dx1 * dy2 - dx2 * dy1;
  if (fabsf(det) < 1e-9f) {
    m.g = m.h = 0;
  } else {
    m.g = (dx3 * dy2 - dx2 * dy3) / det;
    m.h = (dx1 * dy3 - dx3 * dy1) / det;
  }
  m.a = x1 - x0 + m.g * x1;
  m.b = x3 - x0 + m.h * x3;
  m.c = x0;
  m.d = y1 - y0 + m.g * y1;
  m.e = y3 - y0 + m.h * y3;
  m.f = y0;
  return m;
}

static void build_map(const Config &c, int w, int h) {
  float px[8];
  for (int i = 0; i < 4; i++) {
    px[i * 2] = c.corners[i * 2] * (w - 1);
    px[i * 2 + 1] = c.corners[i * 2 + 1] * (h - 1);
  }
  Homography m = square_to_quad(px);

  int n = 0;
  auto add = [&](float u0, float u1, float v0, float v1) {
    if (n >= MAX_LEDS) return;
    int s = 0;
    for (int i = 0; i < GRID; i++) {
      for (int j = 0; j < GRID; j++) {
        float u = u0 + (u1 - u0) * (i + 0.5f) / GRID;
        float v = v0 + (v1 - v0) * (j + 0.5f) / GRID;
        float x, y;
        m.map(u, v, x, y);
        int ix = constrain((int)lroundf(x), 0, w - 1);
        int iy = constrain((int)lroundf(y), 0, h - 1);
        sample_idx[n][s++] = iy * w + ix;
      }
    }
    n++;
  };

  const float d0 = c.inset, d1 = c.inset + c.depth;
  const int nl = c.n_left, nt = c.n_top, nr = c.n_right;
  auto left = [&](int k) { add(d0, d1, (float)k / nl, (float)(k + 1) / nl); };  // k=0 at top
  auto top = [&](int k) { add((float)k / nt, (float)(k + 1) / nt, d0, d1); };   // k=0 at left
  auto right = [&](int k) { add(1 - d1, 1 - d0, (float)k / nr, (float)(k + 1) / nr); };

  if (c.start_corner == 0) {  // bottom-left, up, across, down the right
    for (int k = nl - 1; k >= 0; k--) left(k);
    for (int k = 0; k < nt; k++) top(k);
    for (int k = 0; k < nr; k++) right(k);
  } else {  // bottom-right, up, across, down the left
    for (int k = nr - 1; k >= 0; k--) right(k);
    for (int k = nt - 1; k >= 0; k--) top(k);
    for (int k = 0; k < nl; k++) left(k);
  }

  // Shrinking the strip: blank the LEDs that are no longer driven.
  if (n < n_leds) {
    fill_solid(leds, n_leds, CRGB::Black);
    strip->setLeds(leds, n_leds);
    FastLED.show();
  }
  n_leds = n;
  strip->setLeds(leds, max(n, 1));
}

// ---------------------------------------------------------------- processing

static void process_frame(const camera_fb_t *fb, const Config &c) {
  const uint8_t *px = fb->buf;
  const float alpha = 1.0f - c.smoothing;
  const float bl = c.black_level, bl_scale = 255.0f / (255.0f - bl);

  for (int j = 0; j < n_leds; j++) {
    uint32_t r = 0, g = 0, b = 0;
    for (int s = 0; s < SAMPLES; s++) {
      uint32_t i = sample_idx[j][s] * 2;
      uint16_t p = (px[i] << 8) | px[i + 1];  // RGB565, big-endian
      r += (p >> 8) & 0xF8;
      g += (p >> 3) & 0xFC;
      b += (p << 3) & 0xF8;
    }
    float rgb[3] = {(float)r / SAMPLES, (float)g / SAMPLES, (float)b / SAMPLES};

    // Cut the camera's noise floor so dark scenes go dark, not murky grey.
    for (float &v : rgb) v = max(0.0f, v - bl) * bl_scale;

    // The camera washes colours out; push them away from grey.
    float y = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
    for (float &v : rgb) v = constrain(y + (v - y) * c.saturation, 0.0f, 255.0f);

    const float gains[3] = {c.gain_r, c.gain_g, c.gain_b};
    for (int k = 0; k < 3; k++) {
      float t = gamma_lut[(int)rgb[k]] * gains[k];
      smooth[j][k] += (t - smooth[j][k]) * alpha;
    }
    leds[j] = CRGB(min(255, (int)smooth[j][0]), min(255, (int)smooth[j][1]),
                   min(255, (int)smooth[j][2]));
  }
}

static void processing_task(void *) {
  Config c = ambilight::get_config();
  int fw = 0, fh = 0;
  uint32_t frames = 0, fps_start = millis();

  while (true) {
    if (paused) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool dirty = cfg_dirty;
    if (dirty || fb->width != fw || fb->height != fh) {
      cfg_dirty = false;
      c = ambilight::get_config();
      fw = fb->width;
      fh = fb->height;
      apply_sensor(c);
      build_map(c, fw, fh);
      for (int i = 0; i < 256; i++) gamma_lut[i] = lroundf(255.0f * powf(i / 255.0f, c.gamma));
      FastLED.setBrightness(c.brightness);
      FastLED.setMaxPowerInVoltsAndMilliamps(5, c.max_milliamps);
    }

    if (jpeg_requested) {
      if (!frame2jpg(fb, 80, &jpeg_buf, &jpeg_len)) jpeg_buf = nullptr;
      jpeg_requested = false;
      xSemaphoreGive(jpeg_done);
    }

    if (c.enabled) {
      process_frame(fb, c);
    } else {
      fill_solid(leds, n_leds, CRGB::Black);
    }
    esp_camera_fb_return(fb);
    FastLED.show();

    frames++;
    uint32_t now = millis();
    if (now - fps_start >= 1000) {
      cur_fps = frames * 1000.0f / (now - fps_start);
      frames = 0;
      fps_start = now;
    }
  }
}

// ---------------------------------------------------------------- public

bool ambilight::begin() {
  prefs.begin("ambilight", false);
  Config c = default_config();
  if (prefs.getBytesLength("cfg") == sizeof(Config)) {
    Config stored;
    prefs.getBytes("cfg", &stored, sizeof(stored));
    if (stored.version == CONFIG_VERSION) c = stored;
  }
  sanitize(c);
  cfg = c;

  strip = &FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS);
  FastLED.setCorrection(TypicalSMD5050);
  FastLED.clear(true);

  jpeg_lock = xSemaphoreCreateMutex();
  jpeg_done = xSemaphoreCreateBinary();

  if (!init_camera()) return false;
  xTaskCreatePinnedToCore(processing_task, "ambilight", 8192, nullptr, 2, nullptr, 1);
  return true;
}

int ambilight::led_count() { return n_leds; }

size_t ambilight::get_leds(uint8_t *rgb, size_t max_bytes) {
  size_t n = min<size_t>(n_leds, max_bytes / 3);
  for (size_t i = 0; i < n; i++) {
    rgb[i * 3] = leds[i].r;
    rgb[i * 3 + 1] = leds[i].g;
    rgb[i * 3 + 2] = leds[i].b;
  }
  return n * 3;
}

float ambilight::fps() { return cur_fps; }

bool ambilight::capture_jpeg(uint8_t **buf, size_t *len) {
  if (paused) return false;
  xSemaphoreTake(jpeg_lock, portMAX_DELAY);
  xSemaphoreTake(jpeg_done, 0);  // clear a stale signal from a timed-out request
  jpeg_requested = true;
  bool ok = xSemaphoreTake(jpeg_done, pdMS_TO_TICKS(2000)) == pdTRUE && jpeg_buf;
  if (ok) {
    *buf = jpeg_buf;
    *len = jpeg_len;
  }
  jpeg_buf = nullptr;
  jpeg_requested = false;
  xSemaphoreGive(jpeg_lock);
  return ok;
}

void ambilight::pause(bool p) {
  paused = p;
  if (p) {
    delay(150);  // let the processing task finish its current frame
    FastLED.clear(true);
  }
}
