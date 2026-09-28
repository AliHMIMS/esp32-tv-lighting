# Project rules

## Git
- Commit progressively: one small, focused commit per logical step, as you go. Don't batch a whole session into one commit.
- Never add Claude as a co-author. No `Co-Authored-By: Claude ...` trailer, no "Generated with Claude Code" line, in commits or PRs.
- Default branch is `master`.
- Never commit secrets: `secrets.h` and `secrets.ini` are git-ignored in every sub-project.

## Layout
- `camera/`: archived camera-based prototype (ESP32-S3 + OV2640 watches the TV). Kept for future reference and still builds on its own.
