# ESP32-S3 USB MIDI Foot Controller

A compact **USB-MIDI expression / foot controller** based on an **ESP32-S3**, featuring:

- USB MIDI (class-compliant)
- Analog expression pedal input
- OLED display (SSD1306, 128×64)
- NeoPixel RGB status LED
- Wi-Fi + OTA firmware updates
- Persistent calibration stored in flash (NVS)
- Safe, intentional calibration via BOOT button long-press

Designed for live use: predictable behavior, no accidental recalibration, and clear visual feedback.

---

## Features

- **USB MIDI Control Change**
  - Sends CC messages (default: CC4, Channel 11)
  - Smooth, jitter-free output with filtering and deadband
- **OLED UI**
  - Current CC + channel
  - Large MIDI value readout
  - Progress bar
  - Wi-Fi / IP status
- **NeoPixel feedback**
  - Color gradient based on pedal position
  - Yellow pulse during calibration hold
  - Blue blink during calibration
  - Green confirmation on success
- **Calibration**
  - Stored persistently in flash (NVS)
  - Automatic calibration **only if no saved calibration exists**
  - Manual calibration via **BOOT button hold**
- **OTA updates**
  - Wireless firmware updates over Wi-Fi
  - OTA stays responsive even during calibration

---

## Hardware

Typical setup:

- **ESP32-S3** board with native USB
- **Expression pedal / potentiometer**
  - Connected to an ADC-capable pin
- **SSD1306 OLED (128×64)**
  - I²C (SDA / SCL)
- **NeoPixel (WS2812)**
  - Single RGB LED
- **BOOT button**
  - Uses the board’s built-in `BOOT_PIN` (defined by the ESP32 core)

> ⚠️ The sketch intentionally **does not define `BOOT_PIN`**.  
> It relies on the board variant’s built-in definition for portability.

---

## Calibration behavior (important)

### 1. First boot / fresh flash
- If **no calibration values** are found in flash:
  - The device **automatically enters calibration once**
  - You are prompted to move the pedal through its full range
  - Values are saved to NVS

### 2. Normal operation
- No calibration happens at startup
- The pedal immediately works using stored calibration

### 3. Manual calibration (any time)
- **Hold the BOOT button for 5 seconds**
- While holding:
  - OLED shows a progress bar
  - NeoPixel pulses yellow
- After 5 seconds:
  - Calibration starts
  - Move pedal through full range for ~5 seconds
  - Green LED confirms success
- Release BOOT to return to normal mode

This design prevents accidental recalibration during live use.

---

## MIDI behavior

- Sends **Control Change (CC)** messages over USB
- Default settings (can be changed in code):
  - CC number: `4`
  - MIDI channel: `11`
- Output is:
  - Smoothed (low-pass filtered)
  - Rate-limited
  - Deadbanded to avoid jitter

The device is **class-compliant** and works without drivers on:
- macOS
- Windows
- Linux
- iOS / iPadOS (via USB adapter)

---

## Wi-Fi & OTA

- Connects to Wi-Fi on startup (with timeout)
- OTA is enabled automatically when connected
- Device appears in Arduino IDE under **Network Ports**
- OTA remains responsive during normal operation and calibration

Credentials and OTA password are defined in the sketch.

---

## Notes & design choices

- Calibration is **intentional and explicit**
- BOOT button logic consumes the UI so screens never “flicker”
- MIDI continues running even while holding BOOT
- OLED updates are throttled to avoid I²C artifacts
- No blocking behavior except during the short calibration window

---

## Future ideas

- Non-blocking calibration state machine
- Multiple calibration presets
- Factory reset gesture (e.g. BOOT 10s)
- Configurable CC/channel via BOOT menu

---

## License

MIT (or choose your own)

---

## Author

Built by **Egberts**  
ESP32-S3 USB MIDI Foot Controller
