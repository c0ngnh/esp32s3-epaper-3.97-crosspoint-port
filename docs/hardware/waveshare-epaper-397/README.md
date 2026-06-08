# Waveshare ESP32-S3 ePaper 3.97

CrossPoint board target: PlatformIO env `esp32_s3_epaper_397`.

## Build and flash

```sh
pio run -e esp32_s3_epaper_397
pio run -e esp32_s3_epaper_397 -t upload
```

## Reference files in this folder

| File | Description |
|------|-------------|
| [pinout.txt](./pinout.txt) | GPIO map (display, keys, I2C, SD, audio) |
| [tech-specs.txt](./tech-specs.txt) | Panel and SSD1677 notes |

## Datasheet (not in git)

The full panel datasheet PDF is omitted from the repository (size). Download from Waveshare product documentation for the **3.97" e-Paper** module used on this board.

## More documentation

- [esp32s3-epaper-3.97.md](../../../esp32s3-epaper-3.97.md) — full board + firmware reference  
- [TIPS-397.md](../../TIPS-397.md) — tips & tricks  

## Local vendor tree (optional)

You may clone Waveshare examples into `.waveshare-ref/` at the repo root (gitignored). Not required to build CrossPoint.
