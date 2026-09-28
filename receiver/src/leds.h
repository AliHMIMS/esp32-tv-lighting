#pragma once
#include <Arduino.h>

constexpr int MAX_LEDS = 300;

// Everything tunable from the web UI. Persisted to NVS as a blob, so bump
// CONFIG_VERSION in leds.cpp whenever this layout changes.
struct Config {
  uint32_t version;

  // Strip layout (as seen from the couch)
  uint16_t n_left, n_top, n_right;
  uint8_t start_corner;  // 0 = strip starts bottom-left, 1 = starts bottom-right
  float depth;           // how far into the picture each LED looks, fraction of width/height

  // Colour pipeline
  bool enabled;
  uint8_t brightness;
  float saturation, gamma;
  uint16_t smoothing_ms;  // time constant of the fade between frames
  float gain_r, gain_g, gain_b;
  uint16_t max_milliamps;
  uint16_t timeout_s;  // fade out when no frames arrive for this long
};

struct Status {
  uint32_t frames;
  float fps;
  int width, height;
  uint32_t ms_since_frame;  // UINT32_MAX = never
  bool signal;
};

enum class TestPattern { None, Rainbow, Corners };

namespace leds {

bool begin();
void loop();  // housekeeping from the Arduino loop (deferred config save)

Config get_config();
void set_config(const Config &c);

// Input, called from the network task
void submit_frame(const uint8_t *rgb, int w, int h);
void submit_color(uint8_t r, uint8_t g, uint8_t b);
void clear();

void test_pattern(TestPattern p, uint32_t duration_ms);
void pause(bool p);  // blank the strip and stop output (used during OTA)

size_t get_leds(uint8_t *rgb, size_t max_bytes);  // current colours, strip order
size_t get_preview(uint8_t *rgb, size_t max_bytes, int &w, int &h);  // last frame, downscaled
Status status();

}  // namespace leds
