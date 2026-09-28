# ESP32 TV ambient lighting

Ambient lighting for a TCL 55P8K using an ESP32-S3 and a WS2812B strip (41 left, 73 top, 41 right).

## Projects

- [`receiver/`](receiver/): the main firmware. The ESP32 pretends to be a Hyperion server (FlatBuffers, TCP 19400), so the [Hyperion Android Reborn](https://github.com/evanwhitt/hyperion-android-reborn) grabber app on the TV streams screen frames straight to it. It samples the edges and drives the strip. Setup page with live view and test patterns at `http://tv-ambilight.local/`. `receiver/tools/fake_grabber.py verify` tests it without a TV.
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

The PC's USB port can't supply the Wi-Fi start-up current (brownout loop, reset reason 9). Flashing over USB still works; run the board from a phone charger or the 5 V supply.

## Guides

1. [Power supply wiring](docs/power-supply.md): mains terminals, input voltage check, first power-up
2. [Strip and ESP32 wiring](docs/strip-and-esp32-wiring.md): input end, wires, 330 Ω resistor
3. [Mounting and testing](docs/strip-mounting.md): cutting, where to start, corners test, tuning
4. [TV setup](docs/tv-setup.md): grabber app install and the settings that work on the TCL 55P8K