# Mounting the strip and testing

LEDs go on the left, top and right of the TV back (no bottom), a few cm in from the edges.

## Measure and cut

1. Measure the three runs on the back.
2. LEDs per side = length in metres × 60. A 55" TV lands around **39 left + 69 top + 39 right** (the firmware defaults are 41/73/41; set the real counts on the setup page).
3. Cut only across the copper pads.
4. Corners: cut and bridge with 3 short wires or solderless 3-pin (10 mm) L-connectors, or make a gentle loop. Don't fold sharply: the copper cracks.

"Left" and "right" in the firmware are as seen **from the couch**. Behind the TV they are swapped.

## Where to start

Start at the corner where the ESP32 sits: the data wire from GPIO 14 should stay under ~30 cm.

Default (`Strip starts: Bottom-left` as seen from the front) looks like this from behind:

```
        (looking at the BACK of the TV)
   ┌──────────── ← ← ← top ← ← ← ────────────┐
   ↓                                         ↑
   ↓                                         ↑
 end                                       START (DIN)
 (blue in test)                            (red in test)
   └─────────────────────────────────────────┘
        your left                your right
```

1. Start at the bottom-right (seen from the back) with the input end.
2. Go up, across the top right to left, and down the other side.

To start at the other corner instead, mirror everything and set **Strip starts → Bottom-right** on the setup page.

## Sticking

- Clean the back with isopropyl alcohol first; IP30 adhesive is weak on TV plastic.
- Keep 2–5 cm in from the edges so light hits the wall, not your eyes. Don't cover vents.
- Add adhesive cable clips or tape along the run: factory adhesive tends to let go with TV heat.
- Leave the last few cm near the ends unstuck until the corners test passes.

## Test

On `http://tv-ambilight.local/`:

1. Set **LEDs left / top / right** to the counts you cut.
2. **Test: corners**. Expect:
   - red at the start (bottom-left from the couch)
   - white pairs exactly at the top-left and top-right corners
   - blue at the end (bottom-right from the couch)
3. Red at the bottom-right instead: switch **Strip starts → Bottom-right**.
4. White pairs off the corners: adjust the counts.
5. **Test: rainbow** to check every LED lights.

## Tune

With content playing:

- **Brightness** to taste.
- **White balance**: show a white image and adjust Red/Green/Blue until the wall light is neutral.
- **Saturation** up if colours look pale.
- **Smoothing**: lower is snappier, higher is calmer.
