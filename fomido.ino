#include "USB.h"
#include "USBMIDI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <Wire.h>

// =====================
// Wi-Fi / OTA
// =====================
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "Password";

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 10000;

static const char* OTA_HOSTNAME = "ESP32S3-FootCtrl";
static const char* OTA_PASSWORD = "OTAPassword";

// =====================
// USB / MIDI
// =====================
USBMIDI MIDI;

// =====================
// Hardware Configuration
// =====================
const int POT_PIN   = 4;
const int RGB_PIN   = 48;
const int RGB_COUNT = 1;

// BOOT button (ESP32-S3)
// static constexpr int BOOT_PIN = 0;                 // GPIO0, active LOW
static constexpr uint32_t BOOT_HOLD_MS = 5000;     // 5 seconds

// =====================
// OLED Configuration
// =====================
#define I2C_SDA 9
#define I2C_SCL 10
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// =====================
// MIDI Settings
// =====================
const uint8_t MIDI_CHANNEL   = 11;
const uint8_t CONTROLLER_NUM = 4;

// =====================
// Behavior tuning
// =====================
static constexpr float    SMOOTH_ALPHA  = 0.12f;
static constexpr uint8_t  MIDI_DEADBAND = 1;
static constexpr uint32_t MIDI_RATE_MS  = 5;
static constexpr int      CAL_MIN_SPAN  = 200;

// Hold UI update rate
static constexpr uint32_t HOLD_UI_PERIOD_MS = 33; // ~30 FPS

// =====================
// Globals
// =====================
Adafruit_NeoPixel rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

float smoothRaw = 0;
bool smoothInit = false;

int calibMin = 0;
int calibMax = 4095;

uint8_t lastMidiSent  = 255;
uint8_t lastLedMidi   = 255;
uint8_t lastShownMidi = 255;

uint32_t lastMidiMs = 0;

// BOOT button state
bool bootArmed = true;           // triggers once per press
uint32_t bootPressStart = 0;
uint32_t lastHoldUiMs = 0;
bool holdUiActive = false;

// Wi-Fi retry state
uint32_t lastWifiTryMs = 0;

// =====================
// Calibration storage
// =====================
bool hasSavedCalibration() {
  return prefs.isKey("min") && prefs.isKey("max");
}

bool validateCalibration() {
  int mn = calibMin, mx = calibMax;
  if (mn > mx) { int t = mn; mn = mx; mx = t; }
  return (mx - mn) >= CAL_MIN_SPAN && mn >= 0 && mx <= 4095;
}

void saveCalibration() {
  prefs.putInt("min", calibMin);
  prefs.putInt("max", calibMax);
}

void loadCalibration() {
  if (hasSavedCalibration()) {
    calibMin = prefs.getInt("min", 0);
    calibMax = prefs.getInt("max", 4095);
  } else {
    calibMin = 0;
    calibMax = 4095;
  }
}

// =====================
// RGB
// =====================
void setPedalColor(uint8_t midiValue) {
  uint8_t r, g = 0, b;
  if (midiValue < 64) {
    r = map(midiValue, 0, 63, 0, 128);
    b = map(midiValue, 0, 63, 255, 128);
  } else {
    r = map(midiValue, 64, 127, 128, 255);
    b = map(midiValue, 64, 127, 128, 0);
  }
  rgb.setPixelColor(0, rgb.Color(r, g, b));
  rgb.show();
}

// =====================
// OLED (normal)
// =====================
void updateDisplay(uint8_t midiValue) {
  static uint32_t last = 0;
  if (millis() - last < 100 && midiValue == lastShownMidi) return;
  last = millis();
  lastShownMidi = midiValue;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);

  char top[24];
  snprintf(top, sizeof(top), "CC%u Ch%u", CONTROLLER_NUM, MIDI_CHANNEL);
  u8g2.drawStr(0, 10, top);

  u8g2.setFont(u8g2_font_helvB24_tn);
  char val[5];
  snprintf(val, sizeof(val), "%3u", midiValue);
  int w = u8g2.getStrWidth(val);
  u8g2.drawStr((SCREEN_WIDTH - w) / 2, 38, val);

  int bw = map(midiValue, 0, 127, 0, SCREEN_WIDTH - 10);
  u8g2.drawFrame(4, 44, SCREEN_WIDTH - 8, 10);
  u8g2.drawBox(5, 45, bw, 8);

  u8g2.setFont(u8g2_font_6x12_tr);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    u8g2.drawStr(0, 62, ip.c_str());
  } else {
    u8g2.drawStr(0, 62, "Wi-Fi: offline");
  }

  u8g2.sendBuffer();
}

// =====================
// OLED (hold-to-calibrate UI)
// =====================
void updateHoldUI(float pct) {
  pct = constrain(pct, 0.0f, 1.0f);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvR12_tr);
  u8g2.drawStr(8, 18, "Hold BOOT to");
  u8g2.drawStr(20, 36, "Calibrate (5s)");

  int bw = SCREEN_WIDTH - 16;
  u8g2.drawFrame(8, 48, bw, 10);
  u8g2.drawBox(9, 49, (int)(bw * pct), 8);

  u8g2.setFont(u8g2_font_6x12_tr);
  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    u8g2.drawStr(0, 62, ip.c_str());
  } else {
    u8g2.drawStr(0, 62, "Wi-Fi: offline");
  }

  u8g2.sendBuffer();
}

// =====================
// Calibration (blocking ~5s)
// =====================
void calibratePedal(bool force) {
  if (!force) return;

  calibMin = 4095;
  calibMax = 0;

  uint32_t start = millis();
  while (millis() - start < 5000) {
    // Best-effort keep OTA alive during calibration
    if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

    int v = analogRead(POT_PIN);
    calibMin = min(calibMin, v);
    calibMax = max(calibMax, v);

    bool on = ((millis() / 200) % 2);
    rgb.setPixelColor(0, on ? rgb.Color(0, 0, 255) : 0);
    rgb.show();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvR12_tr);
    u8g2.drawStr(18, 30, "Calibrating...");
    u8g2.sendBuffer();

    delay(10);
  }

  if (!validateCalibration()) {
    calibMin = 0;
    calibMax = 4095;
  } else {
    saveCalibration();
  }

  rgb.setPixelColor(0, rgb.Color(0, 255, 0));
  rgb.show();
  delay(500);
  rgb.clear();
  rgb.show();

  // Force refresh after returning to normal mode
  lastShownMidi = 255;
  lastLedMidi   = 255;
  lastMidiSent  = 255;
}

// =====================
// BOOT button handler
// (only triggers calibration after 5s hold)
// =====================
void handleBootButtonCalibration() {
  bool pressed = (digitalRead(BOOT_PIN) == LOW);

  if (!pressed) {
    bootPressStart = 0;
    bootArmed = true;
    lastHoldUiMs = 0;
    if (holdUiActive) {
      lastShownMidi = 255;
      lastLedMidi = 255;
    }
    holdUiActive = false;
    return;
  }

  holdUiActive = true;

  // If already triggered, wait for release
  if (!bootArmed) return;

  if (bootPressStart == 0) bootPressStart = millis();
  uint32_t held = millis() - bootPressStart;

  // Throttle hold UI + LED
  uint32_t now = millis();
  if (now - lastHoldUiMs >= HOLD_UI_PERIOD_MS) {
    lastHoldUiMs = now;

    float pct = (float)held / (float)BOOT_HOLD_MS;
    updateHoldUI(pct);

    // Pulsing yellow LED while holding
    uint32_t t = now % 600;
    uint8_t wave = (t < 300) ? t : (600 - t);
    float base = 0.2f + 0.8f * constrain(pct, 0.0f, 1.0f);
    float pulse = 0.4f + 0.6f * ((float)wave / 300.0f);
    float b = constrain(base * pulse, 0.0f, 1.0f);

    rgb.setPixelColor(0, rgb.Color((uint8_t)(255 * b), (uint8_t)(200 * b), 0));
    rgb.show();
  }

  // Trigger once after 5s
  if (held >= BOOT_HOLD_MS) {
    bootArmed = false;
    bootPressStart = 0;
    calibratePedal(true);
  }
}

// =====================
// Wi-Fi / OTA helpers
// =====================
void connectWiFiWithTimeout() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWi-Fi connect timed out. Continuing without Wi-Fi/OTA.");
  }
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastWifiTryMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiTryMs = now;

  Serial.println("Wi-Fi disconnected; retrying...");
  WiFi.disconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void setupOTAIfConnected() {
  if (WiFi.status() != WL_CONNECTED) return;

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA End"); });
  ArduinoOTA.onError([](ota_error_t err) { Serial.printf("OTA Error[%u]\n", err); });

  ArduinoOTA.begin();
  Serial.println("OTA ready.");
}

// =====================
// Setup / Loop
// =====================
void setup() {
  Serial.begin(115200);
  delay(100);

  // USB MIDI
  USB.begin();
  MIDI.begin();

  // ADC + pins
  analogReadResolution(12);
  pinMode(POT_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // NeoPixel
  rgb.begin();
  rgb.setBrightness(50);
  rgb.clear();
  rgb.show();

  // NVS
  prefs.begin("pedalcal", false);
  bool hadCal = hasSavedCalibration();
  loadCalibration();

  // OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(200000);
  u8g2.begin();

  // Wi-Fi + OTA
  connectWiFiWithTimeout();
  setupOTAIfConnected();

  // If no saved calibration exists, calibrate once at startup
  if (!hadCal) {
    calibratePedal(true);
  }
}

void loop() {
  // Keep Wi-Fi/OTA alive
  maintainWiFi();
  if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

  // Handle BOOT press UI / trigger calibration (5s hold)
  handleBootButtonCalibration();

  // ----- Always keep MIDI running -----
  int raw = analogRead(POT_PIN);
  if (!smoothInit) {
    smoothRaw = raw;
    smoothInit = true;
  } else {
    smoothRaw += SMOOTH_ALPHA * (raw - smoothRaw);
  }

  int mn = calibMin, mx = calibMax;
  if (mn > mx) { int t = mn; mn = mx; mx = t; }
  if (mx - mn < CAL_MIN_SPAN) { mn = 0; mx = 4095; }

  uint8_t midi = map(constrain((int)smoothRaw, mn, mx), mn, mx, 0, 127);

  if (lastMidiSent == 255) lastMidiSent = midi;

  if (abs((int)midi - (int)lastMidiSent) > MIDI_DEADBAND &&
      (millis() - lastMidiMs) > MIDI_RATE_MS) {
    MIDI.controlChange(CONTROLLER_NUM, midi, MIDI_CHANNEL);
    lastMidiSent = midi;
    lastMidiMs = millis();
  }

  // ----- UI updates -----
  // If hold UI is active, don't overwrite it with normal UI/LED.
  if (!holdUiActive) {
    if (midi != lastLedMidi) {
      setPedalColor(midi);
      lastLedMidi = midi;
    }
    updateDisplay(midi);
  }
}
