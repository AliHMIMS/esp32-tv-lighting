#pragma once
#include <Arduino.h>

// Minimal Hyperion FlatBuffers server (the protocol Hyperion.ng listens to on
// port 19400). Screen grabbers such as Hyperion Android Reborn connect here
// and stream RawImage frames, which are forwarded to leds::submit_frame().
namespace hyperion {

constexpr uint16_t PORT = 19400;

bool begin();
String client();  // "ip (origin)" of the connected grabber, or "" if none

}  // namespace hyperion
