# Strip and ESP32 wiring

WS2812B, 5 V, 60 LEDs/m, IP30. Data comes from ESP32 **GPIO 14**.

## Find the input end

Data only flows one way. The arrows printed on the strip point **away** from the input end.

```
 INPUT END (connect here)                                   OUTPUT END
     │                                                          │
     ▼                                                          ▼
  ┌──────────────────────────────────────────────────────────────┐
  │ 5V  ┌───┐  →   ┌───┐  →   ┌───┐  →   ┌───┐  →   ┌───┐    5V │
  │ DIN │LED│  →   │LED│  →   │LED│  →   │LED│  →   │LED│   DO  │
  │ GND └───┘  →   └───┘  →   └───┘  →   └───┘  →   └───┘   GND │
  └──────────────────────────────────────────────────────────────┘
          ─────────── arrows point this way ──────────►
                     (direction the data travels)
```

- Input end: pads labelled `DIN` / `DI`, arrows start here.
- Output end: pads labelled `DO` / `DOUT`, arrows point toward it.
- Hold the strip with the arrows pointing right: the input is on your left.

## The wires at the input end

| Wire | Meaning | Carries |
|---|---|---|
| Loose red | 5 V | main power feed for the strip |
| Loose white | GND | main power feed for the strip |
| Connector red | 5 V | same net as loose red, thin wire |
| Connector white | GND | same net as loose white, thin wire |
| Connector green | Data | signal from the ESP32 |

The loose pair exists so the strip's current doesn't have to go through the thin connector wires.

## Wiring

```
PSU +V  ──────────── loose RED    (strip power)
PSU -V  ──────────── loose WHITE  (strip power)

Connector RED   ──────────────── ESP32 5V pin
Connector WHITE ──────────────── ESP32 GND
Connector GREEN ───[330Ω]─────── ESP32 GPIO 14
```

The ESP32 is powered from the strip: the PSU feeds the strip through the thick loose wires, and the ESP32 draws its fraction of an amp through the connector. One 3-wire link, and the common ground comes for free.

### Connector

- The strip usually ships with a matching male 3-pin pigtail; use it.
- Without one: Dupont jumpers pushed into the female connector work for testing. For the final install use a 3-pin JST SM pigtail or solder.

### Power injection at the far end

For even colours along the run, also connect the strip's 5 V and GND at the **end** of the last side to the PSU `+V`/`-V`. Use ~1 mm² (18 AWG) for power runs; any thin wire is fine for data.

Optional: a 1000 µF capacitor across 5 V/GND at the strip start.

## The 330 Ω resistor

A resistor has no polarity: either way round works. It goes **in series** in the data line, close to the ESP32.

```
 ESP32                                                      STRIP
 GPIO 14 ●────────────┤▒▒▒▒▒▒├────────────● green wire (DIN)
                       330Ω resistor
                    (either way round)
```

```
  wire from GPIO 14      resistor (orange-orange-brown bands)      green wire
  ════════════════╗    ┌────────────────┐    ╔════════════════
                  ╚════┤ ▌▌  ▌▌  ▌  ▐▐  ├════╝
                  twist └────────────────┘ twist
                  + solder                 + solder
```

1. Place it within a few cm of GPIO 14.
2. Twist or (better) solder each leg to a wire. A breadboard row works for testing.
3. Cover the resistor and joints with heat-shrink or tape.

330 Ω reads orange-orange-brown (4-band) or orange-orange-black-black (5-band). Anything 220–470 Ω works.

## Before powering on

1. Polarity: red only to `+V` / ESP32 5V, white only to `-V` / GND. Reversed 5 V kills the strip and the ESP32 instantly.
2. **Never connect USB and the PSU to the ESP32 at the same time.** Updates go over Wi-Fi.
3. Brightness is capped by the firmware (8 A power limit, adjustable on the setup page).

If the first LED flickers or shows wrong colours, the 3.3 V data signal is marginal: add a 74AHCT125 level shifter.

After power-on the ESP32 comes up at `http://tv-ambilight.local/` within ~10 s. Next: [mounting and testing](strip-mounting.md).
