#include "leds.h"

#include <FastLED.h>
#include <Preferences.h>

#define LED_PIN 14

static constexpr uint32_t CONFIG_VERSION = 1;
static constexpr int MAX_SAMPLES_PER_AXIS = 8;  // cap per-LED work on big (codec) frames
static constexpr int PREVIEW_MAX = 96;
static constexpr uint32_t FADE_OUT_MS = 600;

static Config cfg;
static portMUX_TYPE cfg_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool cfg_dirty = true;
static volatile uint32_t save_due = 0;  // millis() when to persist, 0 = nothing pending
static Preferences prefs;
static uint8_t gamma_lut[256];

static CRGB pixels[MAX_LEDS];
static CLEDController *strip = nullptr;
static float current[MAX_LEDS][3];  // what the strip shows, before FastLED brightness

// Shared between the network task (writer) and the output task / web (readers)
static SemaphoreHandle_t lock;
static float target[MAX_LEDS][3];
static uint8_t preview[PREVIEW_MAX * PREVIEW_MAX * 3];
static int preview_w = 0, preview_h = 0;
static int frame_w = 0, frame_h = 0;
static uint32_t frames = 0, last_frame_ms = 0;
static bool have_signal = false;
static float cur_fps = 0;

static volatile bool paused = false;
static volatile TestPattern test = TestPattern::None;
static volatile uint32_t test_until = 0;

// ---------------------------------------------------------------- config

static Config default_config() {
  Config c = {};
  c.version = CONFIG_VERSION;
  c.n_left = 41;
  c.n_top = 73;
  c.n_right = 41;
  c.start_corner = 0;
  c.depth = 0.1f;
  c.enabled = true;
  c.brightness = 180;
  c.saturation = 1.1f;
  c.gamma = 1.8f;
  c.smoothing_ms = 80;
  c.gain_r = 1.0f;
  c.gain_g = 0.9f;
  c.gain_b = 0.8f;
  c.max_milliamps = 8000;  // 10A supply, leave headroom for the ESP32
  c.timeout_s = 5;
  return c;
}

static void sanitize(Config &c) {
  c.version = CONFIG_VERSION;
  c.n_left = min<int>(c.n_left, MAX_LEDS);
  c.n_top = min<int>(c.n_top, MAX_LEDS - c.n_left);
  c.n_right = min<int>(c.n_right, MAX_LEDS - c.n_left - c.n_top);
  c.start_corner = c.start_corner ? 1 : 0;
  c.depth = constrain(c.depth, 0.01f, 0.5f);
  c.saturation = constrain(c.saturation, 0.0f, 4.0f);
  c.gamma = constrain(c.gamma, 0.5f, 4.0f);
  c.smoothing_ms = min<int>(c.smoothing_ms, 2000);
  c.gain_r = constrain(c.gain_r, 0.0f, 2.0f);
  c.gain_g = constrain(c.gain_g, 0.0f, 2.0f);
  c.gain_b = constrain(c.gain_b, 0.0f, 2.0f);
  c.max_milliamps = constrain(c.max_milliamps, 200, 9500);
  c.timeout_s = constrain(c.timeout_s, 1, 3600);
}

static int led_total(const Config &c) { return c.n_left + c.n_top + c.n_right; }

static void build_gamma(float gamma) {
  for (int i = 0; i < 256; i++) gamma_lut[i] = lroundf(255.0f * powf(i / 255.0f, gamma));
}

Config leds::get_config() {
  portENTER_CRITICAL(&cfg_mux);
  Config c = cfg;
  portEXIT_CRITICAL(&cfg_mux);
  return c;
}

void leds::set_config(const Config &in) {
  Config c = in;
  sanitize(c);
  portENTER_CRITICAL(&cfg_mux);
  cfg = c;
  portEXIT_CRITICAL(&cfg_mux);
  build_gamma(c.gamma);
  cfg_dirty = true;
  save_due = millis() + 3000;  // sliders fire often; don't wear out flash
}

void leds::loop() {
  if (save_due && (int32_t)(millis() - save_due) >= 0) {
    save_due = 0;
    Config c = get_config();
    prefs.putBytes("cfg", &c, sizeof(c));
    Serial.println("Config saved");
  }
}

// ---------------------------------------------------------------- input

static void process_color(const Config &c, float r, float g, float b, float *out) {
  float y = 0.299f * r + 0.587f * g + 0.114f * b;
  float rgb[3] = {r, g, b};
  const float gains[3] = {c.gain_r, c.gain_g, c.gain_b};
  for (int k = 0; k < 3; k++) {
    float v = constrain(y + (rgb[k] - y) * c.saturation, 0.0f, 255.0f);
    out[k] = gamma_lut[(int)v] * gains[k];
  }
}

void leds::submit_frame(const uint8_t *rgb, int w, int h) {
  static float next[MAX_LEDS][3];  // only the network task calls this
  const Config c = get_config();
  int n = 0;

  // Average a pixel rectangle [x0,x1) x [y0,y1), sampling at most
  // MAX_SAMPLES_PER_AXIS^2 points so large frames stay cheap.
  auto region = [&](int x0, int x1, int y0, int y1) {
    if (n >= MAX_LEDS) return;
    x0 = constrain(x0, 0, w - 1);
    x1 = constrain(x1, x0 + 1, w);
    y0 = constrain(y0, 0, h - 1);
    y1 = constrain(y1, y0 + 1, h);
    const int sx = max(1, (x1 - x0) / MAX_SAMPLES_PER_AXIS);
    const int sy = max(1, (y1 - y0) / MAX_SAMPLES_PER_AXIS);
    uint32_t r = 0, g = 0, b = 0, count = 0;
    for (int y = y0; y < y1; y += sy) {
      const uint8_t *p = rgb + ((size_t)y * w + x0) * 3;
      for (int x = x0; x < x1; x += sx, p += sx * 3) {
        r += p[0];
        g += p[1];
        b += p[2];
        count++;
      }
    }
    process_color(c, (float)r / count, (float)g / count, (float)b / count, next[n++]);
  };

  const int dw = max(1, (int)lroundf(c.depth * w));
  const int dh = max(1, (int)lroundf(c.depth * h));
  const int nl = c.n_left, nt = c.n_top, nr = c.n_right;
  auto left = [&](int k) { region(0, dw, k * h / nl, (k + 1) * h / nl); };  // k=0 at top
  auto top = [&](int k) { region(k * w / nt, (k + 1) * w / nt, 0, dh); };   // k=0 at left
  auto right = [&](int k) { region(w - dw, w, k * h / nr, (k + 1) * h / nr); };

  if (c.start_corner == 0) {  // bottom-left, up, across, down the right
    for (int k = nl - 1; k >= 0; k--) left(k);
    for (int k = 0; k < nt; k++) top(k);
    for (int k = 0; k < nr; k++) right(k);
  } else {  // bottom-right, up, across, down the left
    for (int k = nr - 1; k >= 0; k--) right(k);
    for (int k = nt - 1; k >= 0; k--) top(k);
    for (int k = 0; k < nl; k++) left(k);
  }

  // Downscaled copy of the frame for the web preview (nearest neighbour).
  const int big = max(w, h);
  const int pw = big > PREVIEW_MAX ? max(1, w * PREVIEW_MAX / big) : w;
  const int ph = big > PREVIEW_MAX ? max(1, h * PREVIEW_MAX / big) : h;

  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(target, next, sizeof(float) * 3 * n);
  for (int y = 0; y < ph; y++) {
    for (int x = 0; x < pw; x++) {
      const uint8_t *src = rgb + ((size_t)(y * h / ph) * w + x * w / pw) * 3;
      memcpy(preview + (y * pw + x) * 3, src, 3);
    }
  }
  preview_w = pw;
  preview_h = ph;
  frame_w = w;
  frame_h = h;
  frames++;
  last_frame_ms = millis();
  have_signal = true;
  xSemaphoreGive(lock);
}

void leds::submit_color(uint8_t r, uint8_t g, uint8_t b) {
  const Config c = get_config();
  float out[3];
  process_color(c, r, g, b, out);
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int j = 0; j < MAX_LEDS; j++) memcpy(target[j], out, sizeof(out));
  last_frame_ms = millis();
  have_signal = true;
  xSemaphoreGive(lock);
}

void leds::clear() {
  xSemaphoreTake(lock, portMAX_DELAY);
  have_signal = false;
  xSemaphoreGive(lock);
}

// ---------------------------------------------------------------- output

static void apply_config(const Config &c) {
  int n = led_total(c);
  static int shown = 0;
  if (n < shown) {  // strip got shorter: blank the LEDs we stop driving
    fill_solid(pixels, shown, CRGB::Black);
    strip->setLeds(pixels, shown);
    FastLED.show();
  }
  shown = n;
  strip->setLeds(pixels, max(n, 1));
  FastLED.setBrightness(c.brightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, c.max_milliamps);
}

static void render_test(const Config &c, int n) {
  static uint8_t hue = 0;
  fill_solid(pixels, n, CRGB::Black);
  if (test == TestPattern::Rainbow) {
    fill_rainbow(pixels, n, hue += 2, max(1, 255 / max(n, 1)));
    return;
  }
  // Corners: first LED red, last LED blue, the ends of each side white.
  const int a = c.start_corner == 0 ? c.n_left : c.n_right;
  const int b = c.n_top;
  for (int i : {a - 1, a, a + b - 1, a + b}) {
    if (i >= 0 && i < n) pixels[i] = CRGB::White;
  }
  if (n > 0) pixels[0] = CRGB::Red;
  if (n > 1) pixels[n - 1] = CRGB::Blue;
}

static void output_task(void *) {
  TickType_t wake = xTaskGetTickCount();
  uint32_t prev = millis(), fps_frames = 0, fps_start = millis();

  while (true) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(16));  // ~60 Hz
    if (paused) continue;

    const Config c = leds::get_config();
    if (cfg_dirty) {
      cfg_dirty = false;
      apply_config(c);
    }
    const int n = led_total(c);
    const uint32_t now = millis();
    const float dt = now - prev;
    prev = now;

    if (test != TestPattern::None && (int32_t)(now - test_until) < 0) {
      render_test(c, n);
      FastLED.show();
      continue;
    }
    test = TestPattern::None;

    xSemaphoreTake(lock, portMAX_DELAY);
    if (have_signal && now - last_frame_ms > c.timeout_s * 1000u) have_signal = false;
    const bool on = c.enabled && have_signal;
    const float tau = on ? c.smoothing_ms : FADE_OUT_MS;
    const float alpha = tau > 0 ? 1.0f - expf(-dt / tau) : 1.0f;
    for (int j = 0; j < n; j++) {
      for (int k = 0; k < 3; k++) {
        const float t = on ? target[j][k] : 0.0f;
        current[j][k] += (t - current[j][k]) * alpha;
      }
      pixels[j] = CRGB(min(255, (int)lroundf(current[j][0])), min(255, (int)lroundf(current[j][1])),
                     min(255, (int)lroundf(current[j][2])));
    }

    // Incoming frame rate, measured over ~1 s windows.
    if (now - fps_start >= 1000) {
      cur_fps = (frames - fps_frames) * 1000.0f / (now - fps_start);
      fps_frames = frames;
      fps_start = now;
    }
    xSemaphoreGive(lock);

    FastLED.show();
  }
}

// ---------------------------------------------------------------- public

bool leds::begin() {
  prefs.begin("leds", false);
  Config c = default_config();
  if (prefs.getBytesLength("cfg") == sizeof(Config)) {
    Config stored;
    prefs.getBytes("cfg", &stored, sizeof(stored));
    if (stored.version == CONFIG_VERSION) c = stored;
  }
  sanitize(c);
  cfg = c;
  build_gamma(c.gamma);

  lock = xSemaphoreCreateMutex();
  strip = &FastLED.addLeds<WS2812B, LED_PIN, GRB>(pixels, MAX_LEDS);
  FastLED.setCorrection(TypicalSMD5050);
  FastLED.clear(true);

  // Headroom for FastLED's RMT setup and power-limit maths on show().
  xTaskCreatePinnedToCore(output_task, "leds", 8192, nullptr, 3, nullptr, 1);
  return true;
}

void leds::test_pattern(TestPattern p, uint32_t duration_ms) {
  test_until = millis() + duration_ms;
  test = p;
}

void leds::pause(bool p) {
  paused = p;
  if (p) {
    delay(50);  // let the output task finish its current show()
    FastLED.clear(true);
  }
}

size_t leds::get_leds(uint8_t *rgb, size_t max_bytes) {
  const size_t n = min<size_t>(led_total(get_config()), max_bytes / 3);
  for (size_t i = 0; i < n; i++) {
    rgb[i * 3] = pixels[i].r;
    rgb[i * 3 + 1] = pixels[i].g;
    rgb[i * 3 + 2] = pixels[i].b;
  }
  return n * 3;
}

size_t leds::get_preview(uint8_t *rgb, size_t max_bytes, int &w, int &h) {
  xSemaphoreTake(lock, portMAX_DELAY);
  size_t n = min<size_t>((size_t)preview_w * preview_h * 3, max_bytes);
  memcpy(rgb, preview, n);
  w = preview_w;
  h = preview_h;
  xSemaphoreGive(lock);
  return n;
}

Status leds::status() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Status s;
  s.frames = frames;
  s.fps = cur_fps;
  s.width = frame_w;
  s.height = frame_h;
  s.ms_since_frame = last_frame_ms ? millis() - last_frame_ms : UINT32_MAX;
  s.signal = have_signal;
  xSemaphoreGive(lock);
  return s;
}
