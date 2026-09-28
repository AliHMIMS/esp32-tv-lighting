#pragma once
#include <Arduino.h>

constexpr int MAX_LEDS = 300;

// Everything tunable from the web UI. Persisted to NVS as a blob, so bump
// CONFIG_VERSION in ambilight.cpp whenever this layout changes.
struct Config {
  uint32_t version;

  // TV corners in the camera image, normalized 0..1: TL, TR, BR, BL (x,y pairs)
  float corners[8];

  // Strip layout (as seen from the couch)
  uint16_t n_left, n_top, n_right;
  uint8_t start_corner;  // 0 = strip starts bottom-left, 1 = starts bottom-right

  // Sampling: how deep into the picture each LED looks, and how far to stay
  // off the bezel. Fractions of screen width/height.
  float depth, inset;

  // Colour pipeline
  bool enabled;
  uint8_t brightness;
  float saturation, gamma, smoothing;
  uint8_t black_level;
  float gain_r, gain_g, gain_b;
  uint16_t max_milliamps;

  // Camera sensor
  bool aec, agc, awb;
  uint16_t exposure;
  uint8_t gain;
  bool vflip, hmirror;
};

namespace ambilight {

bool begin();
void loop();  // housekeeping from the Arduino loop (deferred config save)

Config get_config();
void set_config(const Config &c);

int led_count();
size_t get_leds(uint8_t *rgb, size_t max_bytes);  // current colours, strip order
float fps();

// Grabs the next camera frame as JPEG. Caller must free() *buf.
bool capture_jpeg(uint8_t **buf, size_t *len);

void pause(bool p);  // blank LEDs and stop processing (used during OTA)

}  // namespace ambilight
