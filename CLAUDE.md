# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESPHome firmware configs for a Spotpear "Xiaozhi AI Ball" bought on AliExpress (link in `README.md`; version without battery and touch). Meant to be published on GitHub: code comments, docs and on-screen text are in English; only the HA entity names ("Podświetlenie", "Dźwięki alertów") stay Polish, because renaming them would change their entity_ids. `docs/screen.svg` is a hand-drawn mockup of the screen for the README — keep it in sync when the layout or labels change. Never put secrets in tracked files — use `!secret` and list new keys in `secrets.yaml.example`. Built/flashed from the ESPHome add-on in Home Assistant — there is no local ESPHome/Python on this PC.

- `claude-usage-meter.yaml` + `claude_meter.h` — **the active firmware**: Claude usage limits (5h session + weekly), clock and weather on the round screen.

## Hardware (verified by esptool when flashing)

- ESP32-S3 (QFN56) N16R8: 16 MB flash, 8 MB octal PSRAM. Board `esp32-s3-devkitc-1`, ESP-IDF.
- Display: GC9A01A round 240x240 via `mipi_spi`, `invert_colors: true`.
- Pinout is set via `substitutions:` at the top of the YAML. Currently set to Ball **V2** (SPI CLK 4, MOSI 2, CS 5, DC 47, RST 38, backlight GPIO42 inverted). V2 is confirmed working on this unit; the V1 pinout (CLK 14, MOSI 17, CS 13, DC 10, RST 18, backlight GPIO3) left the screen black. Only one back button is usable (BOOT, GPIO0, active-low, strapping pin). Speaker: ES8311 codec on I2C (SDA 15, SCL 14), I2S LRCLK 45 / BCLK 9 / MCLK 16 / DOUT 8, amp enable GPIO46 (pins also in `substitutions:`). Source: github.com/RealDeco/xiaozhi-esphome (`devices/Spotpear Balls`, `devices/Under_Development/Modular/HW/ball_v*_hw.yaml`).

## Commands

Requires a `secrets.yaml` (git-ignored) with the keys from `secrets.yaml.example`: `wifi_ssid`, `wifi_password`, `api_encryption_key`, `ota_password`, `fallback_ap_password`. HA entity ids are built from the `claude_prefix` / `weather_entity` substitutions. In HA: copy the YAML and `claude_meter.h` into `/config/esphome/` and press Install. With the CLI:

```bash
esphome config claude-usage-meter.yaml    # validate YAML only
esphome run claude-usage-meter.yaml       # build + flash (USB or OTA) + stream logs
```

There are no tests; `esphome config` / `compile` is the check.

## Architecture (claude-usage-meter)

- **Data flow is one-way from Home Assistant** (native API, `platform: homeassistant` sensors). Sources: HA integration `trickv/hass-claude-usage` (`sensor.${claude_prefix}_session_usage`, `_week_usage`, `_session_reset_time`, `_weekly_reset_time`, plus unused ones like `_week_usage_pace`, `_api_error`; this unit's prefix is `claude_usage_dawid_pro`) and `${weather_entity}` (`weather.forecast_dom`; state + `temperature` attribute). Time/timezone come from `time: platform: homeassistant`.
- **Widgets are updated with raw LVGL C calls** from lambdas (`lv_label_set_text`, `lv_arc_set_value`, `lv_obj_set_style_*`) via helpers in `claude_meter.h`. Widget ids are cast with `(lv_obj_t *) id(x)` because ESPHome types them per widget (`lv_label_t*`, `lv_arc_t*`, …).
- **Reset countdowns are computed on the device:** reset-time text sensors are parsed from ISO 8601 (`parse_iso`) into `int64_t` globals; the `refresh_time` script redraws clock, date and countdowns every minute and whenever a reset time changes.
- **Layout:** round 240x240, everything `align: CENTER` with x/y offsets; keep content within ~95 px of the centre (inside the inner arc). Outer arc = session (blue), inner arc = weekly (mauve), both 135°→45° so the bottom gap holds a pixel-art Clawd built from plain `obj` rectangles (18x5 grid, 3x6 px per pixel). Column captions "SESSION"/"WEEKLY" sit above the percentages. Usage turns peach at ≥60% and red at ≥85%.
- **Fonts:** big digit fonts use a restricted `glyphs:` list (add any new character there or it won't render); text fonts use `glyphsets: [GF_Latin_Core]` (Latin incl. diacritics). Weather icons are MDI codepoints from the webfont (`font_icon`) — a new icon must be added both to its `glyphs:` and to `weather_icon()` in `claude_meter.h`.
- **Sound:** `i2c` → `audio_dac: es8311` → `speaker: i2s_audio` (external DAC, mono, 16 kHz — same rate as the codec because `rtttl` writes straight to the speaker, no resampler) → `rtttl` (id `buzzer`); amp is an internal `switch: gpio` on GPIO46 (RESTORE_DEFAULT_ON). No audio files — only RTTTL strings. Thresholds (80/95%, re-arm below 10%), quiet hours (23–7) and melodies live in `claude_meter.h`; `alert_step()` advances the restored `session_alert`/`week_alert` globals (0/1/2) so each threshold sounds once per limit window and nothing beeps after a reboot. The `usage` sensors' `on_value` calls it and runs script `alert_sound`, which checks the HA switch "Dźwięki alertów" and quiet hours. Renewal melody only for the session limit; the weekly level resets silently. API action `play_rtttl` (`song_str`) plays any RTTTL from HA and ignores the switch/quiet hours.
- **Colors** follow Catppuccin Mocha (bg `0x1E1E2E`, surface `0x313244`, text `0xCDD6F4`, muted `0x6C7086`, blue `0x89B4FA`, mauve `0xCBA6F7`, peach `0xFAB387`, red `0xF38BA8`, yellow `0xF9E2AF`).
