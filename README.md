# ESP32 TV ambient lighting

Ambient lighting for a TCL 55P8K using an ESP32-S3 and a WS2812B strip (41 left, 73 top, 41 right).

## Projects

- [`camera/`](camera/): camera-based prototype. The board's OV2640 watches the TV, you calibrate the 4 screen corners on a web page, and it samples the edges into LED colours. Archived for now; it works but is sensitive to placement and room light.

## Hardware

- ESP32-S3 N16R8 camera board (Freenove ESP32-S3-WROOM CAM pinout), OV2640
- WS2812B, 5 V, 60 LEDs/m, data on GPIO 14 through a 330 Ω resistor
- 5 V 10 A supply shared by the strip and the board (common ground)

## Building

Each project is a PlatformIO project. From its folder:

```sh
cp include/secrets.h.example include/secrets.h   # Wi-Fi credentials
cp secrets.ini.example secrets.ini               # OTA password
pio run -e usb -t upload    # first flash over USB
pio run -t upload           # then over Wi-Fi (tv-ambilight.local)
```

OTA uploads need an inbound firewall rule for TCP port 40000 on Windows.
