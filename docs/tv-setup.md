# TV setup (TCL 55P8K, Google TV)

The [Hyperion Android Reborn](https://github.com/evanwhitt/hyperion-android-reborn) grabber app captures the screen and streams it to the ESP32 on TCP 19400. DRM-protected apps (Netflix, Disney+, Prime Video...) always capture black; that can't be worked around.

## Install

1. Developer mode: Settings → System → About → click **Android TV OS build** 7 times.
2. Install **Downloader** (AFTVnews) from the Play Store.
3. Settings → Apps → Security & restrictions → **Unknown sources** → allow Downloader.
4. In Downloader, open the latest `app-release.apk` from the [releases page](https://github.com/evanwhitt/hyperion-android-reborn/releases) and install it.

## Settings that work on this TV

| Setting | Value | Why |
|---|---|---|
| Host | `tv-ambilight.local` (or the board's IP) | The app's scan also finds it |
| Port | `19400` | |
| Capture method | **Codec (compatibility)** | Standard capture misses the video layer on TCL: frames come through black and only update when the UI changes |
| Capture size | **Small** (arrives as 64×36) | Plenty for the LEDs; larger codec sizes are MBs per frame and choke Wi-Fi |
| Frame rate | 20–30 | |
| Grab on Boot | on | |

## Video quality matters

The TV's chip can't decode 4K and run the codec capture at the same time. Measured on YouTube:

| Video quality | Capture rate |
|---|---|
| 4K | 2–4 fps, playback stutters |
| 1080p | ~18 fps (16–21), playback smooth |

The LEDs sample a 64×36 image, so 1080p and 4K give identical light. Set YouTube to 1080p; [SmartTube](https://github.com/yuliskov/SmartTube) can make 1080p the default.

## Troubleshooting

- Check `http://tv-ambilight.local/`: status should say **Receiving**, with the TV picture in the live view.
- Live view black while video plays: use Codec capture.
- No screen-recording permission prompt (some TCL firmware hides it): grant it once over ADB with `adb shell appops set com.hyperion.grabber PROJECT_MEDIA allow`.
