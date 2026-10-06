# JC3248W535EN — QR Link Display

Fork of the Spotify-ESP32 board stack for the **JC3248W535EN** (ESP32-S3, 3.5″ AXS15231B QSPI + touch). Shows a full-screen QR code for any URL/text you configure over a WiFi hotspot.

## Features

- SoftAP hotspot `QR-Link-Setup` with captive portal at `http://192.168.4.1`
- Paste a link → QR updates live on the display (saved in NVS)
- Double-tap the screen for info/settings (brightness, AP details, clear link)
- Landscape 480×320 LVGL UI with built-in QR widget

## Board

| Piece | Detail |
|-------|--------|
| MCU | ESP32-S3 @ 240 MHz |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| Panel | 320×480 IPS AXS15231B **QSPI** |
| Touch | AXS15231B **I2C** |
| Backlight | PWM GPIO 1 |

## Quick start

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e JC3248W535EN
sg uucp -c 'pio run -e JC3248W535EN -t upload --upload-port /dev/ttyACM0'
```

1. Join WiFi **QR-Link-Setup** (password `qrlink123`, or scan the on-screen WiFi QR)
2. Open **http://192.168.4.1** (captive portal usually pops automatically)
3. Enter a URL → **Save & Show QR**
4. Double-tap the screen for brightness / system info / clear

## Layout

```text
main/
  main.cpp           # display/touch → SoftAP → QR screen
  board/             # QSPI panel, TE, bounce flush, touch
  wifi/              # SoftAP portal for QR URL
  qr/                # NVS URL store
  ui/                # QR screen + double-tap settings
boards/              # PlatformIO board def
partitions.csv
sdkconfig.defaults   # LVGL QR + WiFi knobs
Spotify-ESP32/       # upstream fork source (kept for reference)
```

## Stack

- PlatformIO + ESP-IDF 5.3.x (`framework = espidf`), C++17
- LVGL 9.3 via Component Manager (`CONFIG_LV_USE_QRCODE=y`)
