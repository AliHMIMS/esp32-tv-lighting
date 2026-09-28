# Power supply wiring

The 5 V 10 A supply powers both the LED strip and the ESP32. The mains side is the dangerous part: work with the cable **unplugged**.

## Terminals

The PSU has 5 screw terminals: `L`, `N`, `⏚` (ground), `-V`, `+V`.

### Mains side (European colour code)

| Cable wire | Meaning | Terminal |
|---|---|---|
| Brown | Live | `L` |
| Blue | Neutral | `N` |
| Green/yellow | Earth | `⏚` |

Green/yellow goes on the ground terminal only, never on `L` or `N`. Always connect it: the metal case relies on it for safety.

### Output side

| Terminal | Goes to |
|---|---|
| `+V` | 5 V: strip red wire (and through it the ESP32 5V pin) |
| `-V` | GND: strip white wire (and through it the ESP32 GND) |

## Input voltage

Check the label and datasheet before the first plug-in.

- **One wide range** such as `85–264VAC`: auto-ranging, fine on any mains.
- **Two ranges** or `110/220V` with a selector switch or a jumper (often `J1`) on the board: it must match your mains. On 220–240 V mains the switch must be on 220/230 V and the jumper **open**. A unit set to 110 V and plugged into 230 V can burn.

Ours: label `AC INPUT 110/220V ±15%`, no selector switch per the datasheet.

## Wiring steps

1. Keep the cable unplugged the whole time.
2. Strip 6–8 mm off each wire. Twist stranded wire tightly, or better, crimp ferrules on, so no loose strand can bridge to the next terminal.
3. Loosen the screw, put the wire under the clamp, tighten firmly. Tug each wire: nothing pulls out, no bare copper outside the terminal.
4. Add strain relief (zip tie or clamp) so a tug on the cable doesn't pull on the terminals.
5. Cover the terminals (the flip-down cover, a printed cover or an enclosure). The screws are live whenever it's plugged in.

## First power-up

Before connecting anything to `+V`/`-V`:

1. Plug in. The small LED on the PSU lights up.
2. Measure `+V` to `-V` with a multimeter: expect 5.0–5.2 V.
3. If needed, turn **VR1** (the output voltage trimmer next to the LED; "VR" = variable resistor) to about **5.1 V**. Never above 5.3 V. VR1 adjusts the *output*, it is not an input voltage selector.
4. Unplug and wait ~10 s (capacitors hold charge) before touching the terminals again.

Never touch the terminals while it's plugged in. If anything smells, buzzes or sparks, unplug immediately.
