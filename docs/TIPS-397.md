# ESP32-S3 ePaper 3.97 — tips & tricks

Practical notes for **CrossPoint Reader** on the Waveshare board. Full reference: [esp32s3-epaper-3.97.md](../esp32s3-epaper-3.97.md).

---

## USB flash / `waiting for download`

If serial shows `boot:0x22 (DOWNLOAD(USB/UART0))` and `waiting for download` **after** upload:

| Step | Action |
|------|--------|
| 1 | Use this tree’s `platformio.ini` — `board_upload.after_reset = watchdog-reset` (not default `hard-reset`). |
| 2 | After `pio run -t upload`, **unplug the battery or USB for 5 s**, reconnect **without** holding BOOT. |
| 3 | Judge success by the **e-paper home screen**, not ROM text on USB. |
| 4 | Open `pio device monitor` only after boot; `monitor_dtr/rts = 0` avoids forcing download mode again. |

`platformio.ini` in this repo already sets those options.

---

## Build & repo size

| Tip | Detail |
|-----|--------|
| **Keep the repo small** | Do not commit `.pio/` or `.cache/` — they are gitignored. Run `clean_for_share` before zipping; a clean tree is ~28 MB of source. |
| **First build is large** | PlatformIO downloads toolchains into `.pio` on your machine only. |
| **Regenerate strings** | After editing `lib/I18n/translations/english.yaml`, run `python3 scripts/gen_i18n.py` before build. |
| **Custom menu icons** | `python3 scripts/convert_icon.py your.png music 32 32` → updates `src/components/icons/music.h` (see script usage). |

---

## Flash size message

After `pio run` you may see:

```text
Total image size: 581xxxx bytes (.bin may be padded larger)
Note: The reported total sizes may be smaller than ...
```

**Safe to ignore.** PlatformIO already reports `Flash: 69.x% (used … from 8388608 bytes)` — that is your **8 MB app partition**. You have ~2.5 MB headroom before you must trim features.

Partition layout (`partitions.csv`):

| Partition | Size |
|-----------|------|
| `app0` / `app1` | 6 MB each — dual OTA firmware banks |
| `spiffs` | ~4 MB — reserved (CrossPoint uses SD for books/media) |
| `nvs` | 20 KB — settings |

---

## SD card

- Use **FAT32**; exFAT is not supported by default.
- Folders auto-created: `/pictures`, `/music`, `/recordings`.
- Put EPUBs anywhere; open via **Browse files**.
- Sleep wallpapers: `/pictures/sleep_bg.png` (or `.bmp`/`.jpg`); power off: `shutdown_bg.*`. If missing, a random image from `/pictures` is used. Images **auto-rotate** portrait or landscape to use the full panel (same logic as the picture viewer).
- Wi-Fi upload: enable Wi-Fi in Settings, note the IP shown, open the web UI from a phone/PC on the same network.

---

## Sleep & power

| Action | Result |
|--------|--------|
| **BOOT** hold 3 s | Deep sleep, `sleep_bg` wallpaper, label **SLEEPING** (bottom-left) |
| **Settings → System → Power off** | Deep sleep, `shutdown_bg`, label **POWERED OFF** |
| Wake | **Up**, **Select**, or **Down** only (not BOOT) |
| **PMIC PWR key** | Present on hardware but **not** used for wake/sleep in this firmware — use side keys + BOOT |
| Resume | Usually **Home**; reader if you slept inside a book |

Copy `sleep_bg` / `shutdown_bg` images to `/pictures` on the SD card for custom full-screen art. Landscape photos are rotated automatically when that fills more of the screen.

### Auto-sleep timeout

Settings → sleep timeout still applies, but some screens **block** auto-sleep until you leave or stop the activity:

| Blocks sleep | When |
|--------------|------|
| Music player | Always (while in the app) |
| BMP / picture viewer | Always |
| Web server / Calibre | While the server is running |
| OTA / font download / KOReader auth | While transfer is in progress |
| Clock stopwatch / countdown | While timer is **running** |
| Voice recorder | While **recording** or **playing** a clip |
| Alarm alert | While alarm sound is playing |
| OPDS browser | While **loading** or **downloading** |

---

## Controls (397)

- **Bottom hints hidden** — Back / Up / Down labels are not drawn (more list space). Muscle memory: Up/Down navigate, center = OK, short BOOT = Back.
- **Screenshot (any screen):** hold **Select** (center) + **BOOT** together for about **½ second** → BMP under `/screenshots/` on SD. A short “Screenshot saved” banner appears. In the reader, **turn one page** afterward to full-refresh and clear any banner ghost; on Home/menus the panel clears on its own.
- **Center key:** tap = OK on release; hold 1–2.5 s in reader then release = go home; hold ≥ 2.5 s = full panel refresh (not while BOOT is held).
- **Display cadence:** fast differential refresh between full updates on the SSD1677 panel; full clears ghosting every N updates (Settings → Display → refresh frequency). Long-select (center ≥ 2.5 s) triggers an immediate **full** refresh.
- **Music:** while playing, Up/Down = volume; center = pause. When paused, Up/Down = track list.
- **Music shuffle:** flip the device **180°** (hold inverted ~0.7 s, flip back) to play a random track. Shake-to-delete is **off** during music so speaker vibration does not trigger delete.
- **Shake delete:** triple-shake in file browser, music, recorder, or pictures → confirmation shows **Press OK to delete** and **Press Back to cancel**.

### Clock — countdown

- Home → **Clock** → **Countdown**; default duration **30 s**.
- **Up/Down** to adjust; **hold** for faster steps (range **1 s** to **3 days**).
- **Start** runs the timer; auto-sleep is blocked until it finishes or you pause.
- On expiry: full-screen **COUNTDOWN** alert with the default alarm sound.

---

## Memory (8 MB PSRAM)

The N16R8 module includes **8 MB OPI PSRAM**. Firmware enables it via the `esp32-s3-devkitc1-n16r8` board profile; large scratch buffers (e.g. EPUB BW backup chunks) prefer SPIRAM when allocated.

Serial log every 10 s (and once at boot):

- `MEM heap internal free=… largest=… | spiram free=… largest=…`

If `spiram free` is near zero, check that you flashed the **N16R8** profile (not 8 MB DevKitC without PSRAM).

---

## Battery (AXP2101)

- Status bar % comes from the PMIC fuel gauge, VBAT ADC, or VSYS fallback.
- Charge current target **700 mA** when the PMIC accepts it (**500 mA** fallback). Serial: `AXP charge current: … mA`.
- Serial after boot: `AXP init: present=… vbat=… vsys=… vbus=… soc=…`
- **`present=0`, all mV = 0:** no cell detected — check the MX1.25 battery connector or use USB only (firmware may show 100% when VBUS is good).
- **`present=1`, low mV:** charge the pack; see charge state in Settings / serial `chg=`.
- Do not use GPIO ADC for battery on this board.

---

## Environment sensor (SHTC3)

- Top bar shows °C and %RH when the sensor reads OK.
- Serial: look for `SHTC3 ready:` after boot (not `soft reset failed` before init).
- If missing: IMU and SHTC3 share I²C — firmware pauses IMU during env reads; reflash latest `HalBoard397` if an old build probes too early.
- ~4 °C offset applied for PCB heat near the panel.

---

## Audio

- Music: WAV/MP3 in `/music`.
- Recorder: WAV in `/recordings` (24 kHz mono).
- Set volume **while music is playing** — level is saved for all playback.
- If you hear a click on exit/sleep, flash a recent firmware build (ES8311 anti-pop sequencing is included).

---

## Wi-Fi & time

- **Settings → System → Auto time sync:** NTP when Wi-Fi is up; timezone from IP when available.
- Manual date/time when auto sync is off.
- OTA / SD firmware update: see Settings (if enabled in your build).

---

## Customizing firmware

1. Edit sources under `src/` or `lib/hal/`.
2. `pio run -e esp32_s3_epaper_397`
3. `pio run -e esp32_s3_epaper_397 -t upload`

397-only code is guarded with `#if defined(BOARD_ESP32_S3_EPAPER_397)` or `gpio.deviceIsEpaper397()`.

Key files:

| Change | File |
|--------|------|
| Pins | `lib/hal/HalGPIO.h` |
| Board init / SHTC3 | `lib/hal/HalBoard397.cpp` |
| Sleep UI | `src/activities/boot_sleep/SleepActivity.cpp` |
| Hide/show button hints | `src/components/themes/*Theme.cpp`, `BaseTheme.h` (`kThemeButtonHintsHeight`) |
| Home menu | `src/activities/home/HomeActivity.cpp` |

---

## Vendor examples (optional)

Waveshare Arduino/ESP-IDF examples are **not** shipped in this repo (hundreds of MB). Download from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-ePaper-3.97) if you need bare-metal panel/audio tests. CrossPoint already integrates the same drivers via `open-x4-sdk` and `lib/hal/`.

---

## Troubleshooting quick hits

| Problem | Try |
|---------|-----|
| Won't boot after flash | Board must be **N16R8** (16 MB + PSRAM), env `esp32_s3_epaper_397` |
| No USB port | Hold **BOOT**, plug USB |
| No temp/humidity | Check serial for SHTC3; reseat SD, power-cycle |
| Wake only on USB plug | Unplug USB; test on battery — log `USB_UART_CHIP_RESET` is not GPIO wake |
| Vietnamese missing glyphs | Switch UI language or add font coverage |
| Battery always 0% | Check `AXP init:` line; reflash latest `HalBoard397`; verify battery FPC |
| Music “shake” logs, no shuffle | Use **flip 180°**, not shake; update build with music gesture fix |
| Screen flickers on many pages | Update to a recent firmware build (render coalescing) |

More: [esp32s3-epaper-3.97.md § Troubleshooting](../esp32s3-epaper-3.97.md).
