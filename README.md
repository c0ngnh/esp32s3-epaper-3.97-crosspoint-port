# CrossPoint Reader — ESP32-S3 ePaper 3.97 port

Portable **PlatformIO** project for the [Waveshare ESP32-S3-ePaper-3.97](https://www.waveshare.com/esp32-s3-epaper-3.97.htm): a **CrossPoint Reader** port for this board — EPUB reading, Wi-Fi file transfer, settings, OTA, media apps, calendar, alarms, IMU gestures, RTC/NTP, and 397-specific hardware support. For how this differs from official **Xteink X4** firmware, see [Comparison with mainstream CrossPoint Reader](#comparison-with-mainstream-crosspoint-reader).

**Release candidate `1.3.0-WS397-RC`** — Waveshare 3.97 hardware support, reader/menu stability fixes, media tiles, and home UX polish since the `1.3.0-397` dev tag.

| Field | Value |
|-------|--------|
| **Firmware version** | `1.3.0-WS397-RC` (Waveshare 3.97 release candidate) |
| **Last verified build** | **2026-06-08** (`pio run -e esp32_s3_epaper_397`) |
| **Snapshot / backup** | See [BACKUP-SNAPSHOT.txt](BACKUP-SNAPSHOT.txt) |
| **PlatformIO environment** | `esp32_s3_epaper_397` |

---

## Flash firmware

Install pre-built firmware from the [GitHub Releases](https://github.com/c0ngnh/esp32s3-epaper-3.97-crosspoint-port/releases) page — no compiler or PlatformIO required.

Each release includes two `.bin` files. **Use the correct one for your update method:**

| Release file | Use for |
|--------------|---------|
| **`crosspoint-397-<version>-usb-full.bin`** | **USB full flash** via [ESP Launchpad](https://espressif.github.io/esp-launchpad/) at address **`0x0`** (first install or recovery) |
| **`crosspoint-397-<version>-sd-update.bin`** | **SD card update** on the device (Settings → System → SD Card Firmware Update), or **USB app-only flash** via ESP Launchpad at **`0x10000`** (see below) |

**Board:** [Waveshare ESP32-S3-ePaper-3.97](https://www.waveshare.com/esp32-s3-epaper-3.97.htm) only — **not** Xteink X4 or other ESP boards.

### USB flash (ESP Launchpad)

**Requirements:** **Google Chrome** or **Microsoft Edge** (Web Serial / WebUSB; Safari and Firefox are not supported yet), USB-C data cable.

1. Insert a **FAT32** microSD card, connect the board via USB-C (hold **BOOT** if the serial port does not appear).
2. Download **`crosspoint-397-<version>-usb-full.bin`** from the release page.
3. Open **[ESP Launchpad](https://espressif.github.io/esp-launchpad/)** in Chrome or Edge.
4. Scroll to **“Choose your own built firmware image from the local storage to flash and use.”** (DIY mode).
5. Click **Add File** and select `crosspoint-397-<version>-usb-full.bin` with flash address **`0x0`**.
6. Click **Connect** in the top menu and pick your board’s USB serial port. If no port appears, unplug USB, hold **BOOT**, plug in again, then retry **Connect**.
7. Leave **Flashing baud rate** at **921600** (or lower to **460800** if flashing fails).
8. Click **Program** and wait until the console shows success.
9. Click **Reset Device**, or unplug and replug USB. The CrossPoint home screen should appear on the e-paper display.

### USB flash app-only image (ESP Launchpad)

Use **`crosspoint-397-<version>-sd-update.bin`** over USB when the board already has a working bootloader and partition table (e.g. you previously flashed with `-usb-full.bin` or an older release) and you only want to update the application.

1. Download **`crosspoint-397-<version>-sd-update.bin`** from the release page.
2. Open **[ESP Launchpad](https://espressif.github.io/esp-launchpad/)** in Chrome or Edge and connect the board (same steps as above).
3. In DIY mode, click **Add File** and select the `-sd-update.bin` file with flash address **`0x10000`** — **not** `0x0`.
4. Click **Program**, then **Reset Device**.

| File | ESP Launchpad address |
|------|----------------------|
| `-usb-full.bin` | **`0x0`** (bootloader + partition table + app) |
| `-sd-update.bin` | **`0x10000`** (app partition only) |

Flashing `-sd-update.bin` at **`0x0`** overwrites the bootloader and will not boot — recover by re-flashing **`-usb-full.bin`** at **`0x0`**.

### SD card firmware update

Use this when the device already runs CrossPoint and you want to update without a PC.

1. Download **`crosspoint-397-<version>-sd-update.bin`** from the release page — **not** the `-usb-full.bin` file.
2. Copy it to your **FAT32** microSD card (any folder; e.g. `/crosspoint-397-<version>-sd-update.bin`).
3. Insert the SD card and boot the device.
4. Go to **Settings → System → SD Card Firmware Update**.
5. Select the `.bin` file, confirm the update, and wait for the progress bar to finish. **Do not power off** during the update.
6. The device restarts automatically when done.

The device validates the file before flashing. If you pick the USB full image by mistake, it shows **“Use -sd-update.bin, not USB full image”**.

**Recovery mode:** If the device will not boot normally, power on while holding **Up + PWR**. The SD firmware picker opens directly — select the same **`crosspoint-397-<version>-sd-update.bin`** file from the card.

**After flashing (either method)**

- First boot creates `/pictures`, `/music`, and `/recordings` on the SD card if they are missing.
- Copy EPUB files to the SD card, then use **Browse files** on the home menu.

**Troubleshooting**

| Problem | What to try |
|---------|-------------|
| No serial port (USB) | Hold **BOOT** while connecting USB; try another cable or USB port |
| Flash fails / timeout (USB) | Lower baud rate to **460800** or **115200** |
| Blank screen after flash | **Reset Device** in Launchpad, or power-cycle the board |
| SD update rejects the file | You likely picked **`-usb-full.bin`** — use **`-sd-update.bin`** instead |
| Used `-sd-update.bin` in Launchpad at `0x0` | Re-flash **`-usb-full.bin`** at **`0x0`**, or use **`-sd-update.bin`** at **`0x10000`** only |
| Wrong firmware on wrong board | Only use releases from **this** repo on the Waveshare 3.97 — X4 `.bin` files will not work |

---

## Documentation map

| Document | Purpose |
|----------|---------|
| [README.md](README.md) | This file — **flash from release** (top), features, manual, build, backup, **vs upstream** |
| [BACKUP-SNAPSHOT.txt](BACKUP-SNAPSHOT.txt) | Portable backup metadata and rebuild steps |
| [esp32s3-epaper-3.97.md](esp32s3-epaper-3.97.md) | Pinout, wiring, init snippets, controls, troubleshooting |
| [docs/TIPS-397.md](docs/TIPS-397.md) | Tips (SD, sleep, Wi-Fi, audio, UI) |
| [docs/hardware/waveshare-epaper-397/](docs/hardware/waveshare-epaper-397/) | Short GPIO / pin reference |

---

## Hardware features (Waveshare 3.97)

| Component | Model / spec | Role in firmware |
|-----------|----------------|------------------|
| **MCU** | ESP32-S3-WROOM-1 **N16R8** | 16 MB flash, 8 MB OPI PSRAM, 240 MHz |
| **Display** | 3.97″ **800×480** e-paper | SSD1677; portrait reader UI |
| **Storage** | microSD (SDIO) | FAT32 — books, media, settings, wallpapers |
| **PMIC / battery** | **AXP2101** | Charge up to **700 mA** (500 mA fallback), %, VBUS; power-off path |
| **RTC** | **PCF85063** (I²C) | Clock, alarms, calendar |
| **Environment** | **SHTC3** (I²C) | Temp/humidity in status bar (Lyra themes); reader only on RoundedRaff |
| **IMU** | **QMI8658** (I²C) | Tilt page-turn, keyboard nav, shake delete, flip shuffle |
| **Audio** | **ES8311** + **NS4150B** PA | Music (WAV/MP3), voice recorder, alarm sounds |
| **Microphone** | Onboard | Voice recorder |
| **Buttons** | 3× side (Up / Select / Down) + **BOOT** + **PWR** (PMIC) | Primary UI; BOOT = Back / sleep |
| **USB** | Type-C | Flash, serial monitor, charging |

**Important:** Use board profile **esp32-s3-devkitc-1-n16r8** only. Do **not** use the 8 MB DevKitC profile.

---

## Firmware features

### Reading & files

| Feature | Description |
|---------|-------------|
| **EPUB reader** | Full reader at 800×480; **Lyra**, **Lyra Extended**, or **RoundedRaff** UI theme |
| **TXT / XTC** | Additional reader activities where enabled |
| **File browser** | Browse SD; BMP / PNG / JPG viewer |
| **Recent books** | Quick resume from home |
| **Bookmarks & chapters** | EPUB navigation, footnotes, KOReader sync |
| **OPDS** | Online library browse and download (when configured) |
| **Wi-Fi portal** | Web upload (path-validated), settings, fonts (generated HTML) |
| **Calibre / WebDAV** | File transfer integrations |
| **OTA / SD firmware** | Update from network or SD |

### 397 home menu apps

| App | SD paths / notes |
|-----|------------------|
| **Pictures** | `/pictures` — gallery; sleep/shutdown wallpapers |
| **Music** | `/music` — WAV/MP3; volume; flip-180° random track (IMU) |
| **Voice recorder** | `/recordings` — WAV @ 24 kHz |
| **Calendar** | Month grid; Gregorian ↔ Chinese lunar (1901–2099); gyro day pick |
| **Clock** | Alarms (×5), stopwatch, countdown |

### Clock & alarms (397)

| Feature | Description |
|---------|-------------|
| **RTC display** | Large centered `HH:MM` + `SS` tile on clock hub, alarms, and device information |
| **Up to 5 alarms** | Enable, hour, minute, repeat, weekdays, sound, duration |
| **Repeat modes** | **Once** (auto **OFF** after fire; re-enable to arm again), **Daily**, **Weekdays** (per-day picker; no days = disabled) |
| **Alarm sound** | Default beep or file from `/music`; play duration 5–600 s |
| **Alarm alert** | Full-screen alert with animation while sounding |
| **Stopwatch** | Elapsed-time display; blocks auto-sleep while running |
| **Countdown** | Default **30 s**; adjust **1 s–3 days** (hold Up/Down for faster steps); full-screen **COUNTDOWN** alert + default alarm sound on finish; blocks auto-sleep while running |

### Calendar (397)

| Feature | Description |
|---------|-------------|
| **Month view** | Rounded month band + grid; **Up/Down** = prev/next month (year rolls only Jan↔Dec) |
| **Day selection** | **Gyro tilt** moves selected day (like Wi-Fi keyboard); no separate year buttons on 397 |
| **Date card** | Gregorian + lunar summary for selected day |
| **Convert** | Edit Gregorian or lunar fields (browse + edit pattern like clock settings) |

### System & power

| Feature | Description |
|---------|-------------|
| **Status bar** | Battery %, charge; **SHTC3** on Lyra themes (RoundedRaff: reader only) |
| **Date & time** | Manual RTC; timezone; DST; **auto NTP** when Wi-Fi + auto sync on |
| **Deep sleep** | BOOT hold **3 s** or timeout; wake on side keys **4 / 5 / 6** |
| **Power off** | Settings → System → Power off; `shutdown_bg` wallpaper |
| **Sleep screen** | `sleep_bg` / `shutdown_bg` from `/pictures`; auto-rotates portrait/landscape to fill screen (like picture viewer); labels **SLEEPING** / **POWERED OFF** |
| **Auto-sleep** | Settings timeout; blocked while music, BMP viewer, web server, OTA, active countdown/stopwatch, recording/playback, alarm sounding, OPDS download, etc. |
| **Device information** | Firmware, RTC, battery, SHTC3, IMU, SD, RAM — Settings → System |
| **22 UI languages** | Generated from `lib/I18n/translations/*.yaml` |

### Input & gestures (397)

| Gesture | Where used |
|---------|------------|
| **Tilt forward/back** | Reader page turn (optional) |
| **Tilt L/R/U/D** | On-screen keyboard columns/rows; **calendar day grid** |
| **Shake (×3)** | Delete file in browser, music, recorder, pictures; confirm dialog shows **Press OK to delete** / **Press Back to cancel** |
| **Flip 180°** | Random track in music player |
| **Bottom button hints** | Hidden on 397 (more screen area) |
| **Home menu** | Remembers last highlighted item when you return from an app |

---

## Comparison with mainstream CrossPoint Reader

This tree is a **board-specific port**, not a drop-in replacement for official [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) builds aimed at the **Xteink X4**. Both share the same reader core (EPUB stack, Wi-Fi portal, settings model, `open-x4-sdk` display/SD patterns), but they target **different hardware** and ship **different feature sets**.

### At a glance

| | **Mainstream CrossPoint** ([upstream](https://github.com/crosspoint-reader/crosspoint-reader)) | **This port** (`1.3.0-WS397-RC`) |
|---|-----------------------------------------------------------------------------------------------|------------------------------|
| **Primary device** | [Xteink X4](https://github.com/crosspoint-reader/crosspoint-reader) | [Waveshare ESP32-S3-ePaper-3.97](https://www.waveshare.com/esp32-s3-epaper-3.97.htm) only |
| **MCU / memory** | ESP32-**C3** (typical retail X4; ~380 KB usable RAM) | ESP32-**S3** N16R8 — **16 MB** flash, **8 MB** OPI PSRAM |
| **Display** | 4.26″ class panel, **800×480**, SSD1677 family (via [community-sdk](https://github.com/crosspoint-reader/community-sdk)) | 3.97″ **800×480**, SSD1677 (Waveshare `EPD_3in97`) |
| **PlatformIO env** | Upstream device envs (e.g. X4) | **`esp32_s3_epaper_397`** only (`BOARD_ESP32_S3_EPAPER_397`) |
| **Typical version** | e.g. `1.2.x` on GitHub releases | `1.3.0-WS397-RC` (Waveshare 397 port) |
| **Physical controls** | **Four** front keys, remappable in Settings | **Three** side keys (Up / Select / Down) + **BOOT** = Back + **PMIC** power key |
| **Firmware interchange** | Flash **X4** builds on X4 hardware | Flash **this** build on Waveshare 3.97 only — **not** compatible with X4 images |

### Shared with upstream (same CrossPoint DNA)

| Area | Both include |
|------|----------------|
| **Reading** | EPUB 2/3, bookmarks/chapters, footnotes, saved position, TXT/XTC where enabled |
| **Library** | SD file browser, recent books, nested folders, BMP/PNG/JPG viewer |
| **Network** | Wi-Fi setup, web upload portal, OTA, Calibre/WebDAV-style transfer, OPDS (when configured) |
| **Sync & data** | KOReader sync, settings on SD, cache management |
| **UI** | Lyra / Lyra Extended themes; 22 languages from YAML; sleep & cover wallpapers from SD |
| **Power** | Deep sleep, configurable timeouts, reader resume options |
| **SDK** | `open-x4-sdk` symlinks (EInk, SDIO, fonts, EPUB, GfxRenderer, …) |

### Only on this 397 port (highlights)

| Feature | Why it differs |
|---------|----------------|
| **Pictures / Music / Voice recorder** | Home apps backed by `/pictures`, `/music`, `/recordings`; ES8311 codec + PA + mic |
| **Clock hub** | Up to **5 alarms** (Once/Daily/Weekdays, SD sound from `/music`), stopwatch, countdown; PCF85063 RTC |
| **Calendar** | Month grid + **Chinese lunar** (1901–2099); gyro day selection; Gregorian ↔ lunar convert |
| **IMU (QMI8658)** | Tilt page-turn, **gyro keyboard** (Wi-Fi password, calendar), **shake delete**, **flip 180°** → random track |
| **Environment (SHTC3)** | Temp/humidity in status bar (Lyra themes); **Device information** screen |
| **PMIC (AXP2101)** | Accurate battery %, charge/VBUS, hardware power-off path |
| **RoundedRaff theme** | Rounded menus; header env row hidden in apps (reader status bar still shows sensor data) |
| **3-button UX** | No on-screen Back/Up/Down hints; Select hold = home / full refresh; BOOT hold 3 s = sleep |
| **Home menu polish** | Custom icons (music, mic, calendar); **remembers last menu highlight** after Back |
| **Media UI** | Music/recorder tiles with timeline, volume, VBR/Xing MP3 duration probe |
| **USB flash** | `watchdog-reset` after upload (avoids ESP32-S3 stuck in ROM download mode) |

### Mainstream upstream tends to emphasize (397 differs)

| Topic | Mainstream | This port |
|-------|------------|-----------|
| **Target audience** | Official X4 device owners, multi-board upstream repo | Single Waveshare 3.97 board |
| **Button model** | Four remappable front buttons | Fixed 3-side + BOOT mapping; side layout swap for page turn only |
| **Extra home apps** | Reader-centric home (no 397 media/calendar stack) | Five extra 397 apps on home menu |
| **Audio** | Not part of standard X4 CrossPoint feature set | WAV/MP3 playback, recording, alarm sounds |
| **Alarms / lunar calendar** | Not in base upstream feature list | Full clock + calendar activities |
| **RAM budget** | Tighter (C3); single-buffer e-paper modes critical | S3 + PSRAM; larger app partition (~8 MB) |
| **Releases** | [crosspoint-reader releases](https://github.com/crosspoint-reader/crosspoint-reader/releases) for X4 | Built and versioned from **this** repo (`1.3.0-WS397-RC`) |

### Choosing which firmware to use

- **Xteink X4** → use **[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)** official builds and docs.
- **Waveshare ESP32-S3-ePaper-3.97** → download from **[Releases](https://github.com/c0ngnh/esp32s3-epaper-3.97-crosspoint-port/releases)** and flash via USB or SD (see [Flash firmware](#flash-firmware)), or build from source with `pio run -e esp32_s3_epaper_397`.

You can study or merge **source ideas** between projects (both are open source), but **do not flash** an X4 `.bin` on the Waveshare board or vice versa.

---

## Instruction manual (quick start)

### 1. Hardware setup

1. Insert a **FAT32** microSD card.
2. Connect USB-C for power and flashing (hold **BOOT** if the serial port does not appear).
3. Optional: speaker on MX1.25 header for music / alarms.

### 2. Build and flash from source (PC)

Requires [PlatformIO](https://platformio.org/) and internet on **first** build.

**Who needs this section?** Only if you compile the firmware yourself. If you flash a pre-built file from [Releases](#flash-firmware), you do **not** need PlatformIO, Python, or any UTF-8 terminal setup.

```bash
cd esp32s3-epaper-3.97-crosspoint-port
pip install -r scripts/requirements.txt
pio run -e esp32_s3_epaper_397
pio run -e esp32_s3_epaper_397 -t upload
pio device monitor -b 115200
```

**Windows note:** You do **not** need to set `PYTHONIOENCODING=utf-8` for normal builds. The pre-build script `scripts/gen_i18n.py` prints ASCII-only logs so PlatformIO on Windows does not hang on Cyrillic language names. (Release users who only flash pre-built `.bin` files never run this step.)

**Do not commit build output.** Local folders `.pio/`, `.cache/`, and generated files under `lib/I18n/` and `src/network/html/` are listed in [.gitignore](.gitignore) — they stay on your machine only.

Pre-build scripts (automatic on `pio run`):

- `scripts/gen_i18n.py` — UI strings
- `scripts/build_html.py` — Wi-Fi portal pages
- `scripts/patch_jpegdec.py` — JPEG decoder patch
- `scripts/git_branch.py` — optional version suffix when built inside git
- `scripts/check_member_init_order.py` — rejects `% memberVector.size()` in Activity constructor init lists
- `scripts/merge_firmware.py` — post-build merged flash image (`firmware.merged.bin` at `0x0`)

**Developer note:** Do not use member `.size()` in constructor initializer lists; use `static constexpr` counts (see `EpubReaderMenuActivity`).

### 3. First boot on device

1. Boot creates `/pictures`, `/music`, `/recordings` if missing.
2. Copy **EPUB** files to SD → **Browse files**.
3. **Settings → System:** enable **Auto time sync** (Wi-Fi) or set **Date & time** manually.
4. Optional: place `sleep_bg.*` / `shutdown_bg.*` in `/pictures` for custom sleep wallpapers (portrait or landscape — firmware picks the best orientation).

### 4. Buttons (summary)

| Control | Typical action |
|---------|----------------|
| **Up / Down** | List prev/next; month change in calendar; volume in music (playing) |
| **Select** (tap) | Confirm / open |
| **BOOT** (short) | **Back** |
| **BOOT** (hold ≥ 3 s) | **Deep sleep** |
| **Select** (hold 1–2.5 s, reader) | Go home |
| **Select** (hold ≥ 2.5 s) | Full display refresh |

Full control tables: [esp32s3-epaper-3.97.md § Controls](esp32s3-epaper-3.97.md).

### 5. Alarms

1. Home → **Clock** → **Alarms** → pick slot → **Edit**.
2. Set time, **Repeat** (Once / Daily / Weekdays), **Days** (weekday picker if Weekly), sound, duration.
3. **Once:** after the alarm fires, **Enabled** turns **OFF** automatically (re-enable manually to run again).

### 6. Countdown

1. Home → **Clock** → **Countdown**.
2. Set duration (default **30 s**; **Up/Down** to change; hold for faster steps up to **3 days**).
3. **Start** — auto-sleep is paused until the timer finishes or you pause it.
4. On expiry: full-screen **COUNTDOWN** alert with the default alarm sound (same style as clock alarms).

### 7. Calendar

1. Home → **Calendar**.
2. **Gyro tilt** to change the selected day; **Up/Down** to change month.
3. **Select** → **Convert** for Gregorian ↔ lunar editing.

### 8. UI theme (RoundedRaff)

**Settings → Display → UI theme:** **Lyra**, **Lyra Extended**, or **RoundedRaff**. RoundedRaff uses rounded menus and hides the header temp/humidity row (still shown while reading). Env sensor data is always on **Device information**.

---

## Backup and portability

### What to zip

This tree is intended to be **~28 MB** after cleaning (source only). **Do not** zip `.pio` or `.cache` unless you intentionally want a full offline toolchain copy (~2 GB+).

### Clean before backup

**Windows (PowerShell):**

```powershell
.\scripts\clean_for_share.ps1
```

**Linux / macOS:**

```bash
./scripts/clean_for_share.sh
```

Removes:

- `.pio/`, `.cache/` — PlatformIO build and dependencies
- `.vscode/`, `.idea/`, `.cursor/` — IDE / editor settings
- Generated `lib/I18n/I18nKeys.h`, `I18nStrings.*`, `src/network/html/*.generated.h`
- Optional vendor dumps (`docs/ESP32-S3-ePaper-3.97`, `.waveshare-ref`)
- Dev logs and `__pycache__`

**Keeps:** All source, `open-x4-sdk/`, `assets/icons/` (menu PNG sources), `src/components/icons/`, scripts, docs, `platformio.ini`, `.git` (if present).

### Suggested backup name

```text
crosspoint-ws397-v1.3.0-WS397-RC-2026-06-06.zip
```

Record the date and version in [BACKUP-SNAPSHOT.txt](BACKUP-SNAPSHOT.txt) when you create the archive.

### Restore on another PC

1. Unzip the folder.
2. Install PlatformIO (VS Code extension or CLI).
3. `pip install -r scripts/requirements.txt`
4. `pio run -e esp32_s3_epaper_397` (downloads toolchains into local `.pio/`)
5. Flash with `pio run -e esp32_s3_epaper_397 -t upload`

### What is gitignored (normal)

See [.gitignore](.gitignore): `.pio/`, `.cache/`, generated I18n/HTML, IDE folders, local logs.

---

## Project layout

| Path | Contents |
|------|----------|
| `src/` | Application — activities, settings, network, themes, tools |
| `src/activities/tools/` | `ClockActivity`, `CalendarActivity`, … |
| `src/util/` | `CalendarLayout`, `ImageFitUtil`, `LunarCalendar`, `TaskStackMonitor`, `UnifiedAppLayout`, … |
| `lib/hal/` | `HalBoard397`, `HalGPIO`, `HalTiltSensor`, `AlarmSound397`, … |
| `lib/audio397/` | ES8311 codec |
| `lib/EpdFont/`, `lib/Epub/`, `lib/GfxRenderer/`, … | Reader core |
| `lib/I18n/translations/` | YAML UI strings (22 languages) |
| `open-x4-sdk/` | EInk, SDIO, input (symlinked from `platformio.ini`) |
| `scripts/` | Build generators, `clean_for_share` |
| `partitions.csv` | ~8 MB app + SPIFFS on 16 MB flash |
| `platformio.ini` | Env `esp32_s3_epaper_397`, version `1.3.0-WS397-RC` |

---

## Flash / partition note

The build may report total image size vs. partition limit — that is **informational**. This project targets the **8 MB app** partition with comfortable headroom. Details: [docs/TIPS-397.md](docs/TIPS-397.md).

---

## Upstream & lineage

Forked from the CrossPoint Reader ecosystem:

- [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) — mainstream **Xteink X4** firmware
- [community-sdk](https://github.com/crosspoint-reader/community-sdk) — vendored here as `open-x4-sdk/`

This distribution adds **Waveshare 397** hardware support only (`BOARD_ESP32_S3_EPAPER_397`). See [Comparison with mainstream CrossPoint Reader](#comparison-with-mainstream-crosspoint-reader) above.

---

## License

See upstream CrossPoint Reader and bundled component licenses (EPUB stack, fonts, Arduino libraries, etc.).
