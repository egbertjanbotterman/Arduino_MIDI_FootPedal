#include "USB.h"
#include "USBMIDI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <Wire.h>

// =====================
// Wi-Fi Credentials
// =====================
const char* WIFI_SSID = "FritzseBadmuts";
const char* WIFI_PASS = "There is no sp00n!";

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

// =====================
// OLED Configuration
// =====================
#define I2C_SDA 9
#define I2C_SCL 10
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// =====================
// MIDI Settings
// =====================
const uint8_t MIDI_CHANNEL   = 11;
const uint8_t CONTROLLER_NUM = 4;

// =====================
// Tuning / Behavior
// =====================
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 10000;

static constexpr uint32_t READ_INTERVAL_MS        = 5;
static constexpr uint32_t MIDI_RATE_LIMIT_MS      = 5;
static constexpr uint8_t  MIDI_DEADBAND           = 1;

static constexpr float    SMOOTH_ALPHA            = 0.12f;
static constexpr int      CAL_MIN_SPAN            = 200;
static constexpr uint32_t FORCE_CAL_HOLD_MS       = 2000;

// =====================
// Globals
// =====================
Adafruit_NeoPixel rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

uint8_t  lastSentMidi   = 255;
uint8_t  lastShownMidi  = 255;
float    smoothRaw      = 0.0f;
int      calibMin       = 0;
int      calibMax       = 4095;

uint32_t lastReadMs     = 0;
uint32_t lastMidiMs     = 0;
uint32_t lastWifiTryMs  = 0;

// === Pedal Bitmap (64x32, bit-reversed for U8g2 XBM compatibility) ===
#define pedalIcon64_width 64
#define pedalIcon64_height 32
static const unsigned char pedalIcon64_bits[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0xFE,0xFF,0xFF,0x01,
  0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0x03,
  0x00,0x00,0x00,0x00,0xFE,0xFF,0xFF,0x01,
  0x00,0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0x7F,0x00,
  0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0x7F,0x00,
  0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0xFE,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0xFC,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0xF8,0xFF,0xFF,0xFF,0x7F,0x00,
  0x00,0x00,0xE0,0xFF,0xFF,0xFF,0x7F,0x00,
  0x00,0xFF,0xC3,0xFF,0xFF,0xFF,0x7F,0x00,
  0x80,0xFF,0x87,0xFF,0xFF,0xFF,0xFF,0x00,
  0x80,0xFF,0xFF,0x01,0xFC,0xFF,0xFF,0x00,
  0x80,0xFF,0xFF,0x03,0xFC,0xFF,0xFF,0x01,
  0x80,0xFF,0xFF,0x03,0xFC,0xFF,0xFF,0x01,
  0x80,0xFF,0xFF,0x03,0xFC,0xFF,0xFF,0x03,
  0x80,0xFF,0xFF,0x03,0xFC,0xFF,0xFF,0x03,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x03,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,
  0x80,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01
};

// =====================
// Helpers
// =====================
static void setPedalColor(uint8_t midiValue) {
  uint8_t red = 0, green = 0, blue = 0;

  if (midiValue < 64) {
    red  = (uint8_t)map(midiValue, 0, 63, 0, 128);
    blue = (uint8_t)map(midiValue, 0, 63, 255, 128);
  } else {
    red  = (uint8_t)map(midiValue, 64, 127, 128, 255);
    blue = (uint8_t)map(midiValue, 64, 127, 128, 0);
  }
  rgb.setPixelColor(0, rgb.Color(red, green, blue));
  rgb.show();
}

static bool validateCalibration() {
  int mn = calibMin;
  int mx = calibMax;
  if (mn > mx) { int t = mn; mn = mx; mx = t; }
  return (mx - mn) >= CAL_MIN_SPAN && mn >= 0 && mx <= 4095;
}

static void saveCalibration() {
  prefs.putInt("min", calibMin);
  prefs.putInt("max", calibMax);
  Serial.println("Calibration saved to NVS.");
}

static void loadCalibration() {
  if (prefs.isKey("min") && prefs.isKey("max")) {
    calibMin = prefs.getInt("min", 0);
    calibMax = prefs.getInt("max", 4095);
    Serial.println("Loaded calibration from flash.");
  } else {
    calibMin = 0;
    calibMax = 4095;
    Serial.println("No saved calibration found; using defaults.");
  }
}

static void updateDisplay(uint8_t midiValue, bool showCalibrating = false) {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();
  if (now - lastUpdate < 100) return;
  lastUpdate = now;

  if (!showCalibrating && midiValue == lastShownMidi) {
    static uint32_t lastStatusRefresh = 0;
    if (now - lastStatusRefresh < 1000) return;
    lastStatusRefresh = now;
  }
  lastShownMidi = midiValue;

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  if (showCalibrating) {
    u8g2.setFont(u8g2_font_helvR12_tr);
    u8g2.drawStr(18, 24, "Calibrating...");
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(6, 44, "Move pedal full range");
    u8g2.drawStr(18, 60, "Hold heel/toe @ boot");
  } else {
    u8g2.setFont(u8g2_font_6x12_tr);
    char top[24];
    snprintf(top, sizeof(top), "CC%u Ch%u", CONTROLLER_NUM, MIDI_CHANNEL);
    u8g2.drawStr(0, 10, top);

    u8g2.setFont(u8g2_font_helvB24_tn);
    char val[5];
    snprintf(val, sizeof(val), "%3u", midiValue);
    int16_t w = u8g2.getStrWidth(val);
    u8g2.drawStr((SCREEN_WIDTH - w) / 2, 38, val);

    int barWidth = map(midiValue, 0, 127, 0, SCREEN_WIDTH - 10);
    u8g2.drawFrame(4, 44, SCREEN_WIDTH - 8, 10);
    u8g2.drawBox(5, 45, barWidth, 8);

    u8g2.setFont(u8g2_font_6x12_tr);
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      u8g2.drawStr(0, 62, ip.c_str());
    } else {
      u8g2.drawStr(0, 62, "Wi-Fi: offline");
    }
  }

  u8g2.sendBuffer();
}

static void showBootSplash() {
  u8g2.clearBuffer();
  u8g2.drawXBM((SCREEN_WIDTH - pedalIcon64_width) / 2,
               (SCREEN_HEIGHT - pedalIcon64_height) / 2 - 8,
               pedalIcon64_width, pedalIcon64_height, pedalIcon64_bits);

  u8g2.setFont(u8g2_font_helvR10_tr);
  int16_t textWidth = u8g2.getStrWidth("Foot Ctrl v1.1");
  u8g2.drawStr((SCREEN_WIDTH - textWidth) / 2,
               (SCREEN_HEIGHT - pedalIcon64_height) / 2 + 24,
               "Foot Ctrl v1.1");
  u8g2.sendBuffer();
  delay(800);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

static void bootAnimation() {
  for (int i = 0; i <= 127; i += 2) { setPedalColor((uint8_t)i); delay(4); }
  for (int i = 127; i >= 0; i -= 2) { setPedalColor((uint8_t)i); delay(4); }
  rgb.clear(); rgb.show();
}

static bool shouldForceCalibrateAtBoot() {
  Serial.println("Boot: hold pedal at heel OR toe for 2s to force calibration...");

  uint32_t start = millis();
  uint32_t heldStart = 0;
  bool holding = false;

  while (millis() - start < (FORCE_CAL_HOLD_MS + 1200)) {
    int v = analogRead(POT_PIN);
    bool atHeel = (v < 150);
    bool atToe  = (v > 3950);
    bool atExtreme = atHeel || atToe;

    if (atExtreme) {
      if (!holding) {
        holding = true;
        heldStart = millis();
      } else if (millis() - heldStart >= FORCE_CAL_HOLD_MS) {
        Serial.println("Force calibration gesture detected.");
        return true;
      }
    } else {
      holding = false;
    }

    bool on = ((millis() / 150) % 2) == 0;
    rgb.setPixelColor(0, on ? rgb.Color(255, 255, 0) : rgb.Color(0, 0, 0));
    rgb.show();

    updateDisplay(0, true);
    delay(25);
  }

  rgb.clear(); rgb.show();
  Serial.println("No force calibration gesture.");
  return false;
}

static void calibratePedal(bool force = false) {
  bool hasData = prefs.isKey("min") && prefs.isKey("max");
  if (hasData && !force) {
    Serial.println("Using saved calibration.");
    return;
  }

  Serial.println("\n=== Pedal Calibration ===");
  Serial.println("Move pedal full range for 5 s...");

  calibMin = 4095;
  calibMax = 0;

  uint32_t start = millis();
  while (millis() - start < 5000) {
    int val = analogRead(POT_PIN);
    if (val < calibMin) calibMin = val;
    if (val > calibMax) calibMax = val;

    bool on = ((millis() / 200) % 2) == 0;
    rgb.setPixelColor(0, on ? rgb.Color(0, 0, 255) : rgb.Color(0, 0, 0));
    rgb.show();

    updateDisplay(0, true);
    delay(10);
  }

  if (!validateCalibration()) {
    Serial.println("Calibration looked bad (too small span). Using defaults 0..4095.");
    calibMin = 0;
    calibMax = 4095;
  } else {
    saveCalibration();
    Serial.printf("Calibration complete! Min: %d Max: %d\n", calibMin, calibMax);
  }

  rgb.setPixelColor(0, rgb.Color(0, 255, 0));
  rgb.show();
  delay(500);
  rgb.clear(); rgb.show();
}

static void connectWiFiWithTimeout() {
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

static void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastWifiTryMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiTryMs = now;

  Serial.println("Wi-Fi disconnected; retrying...");
  WiFi.disconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void setupOTAIfConnected() {
  if (WiFi.status() != WL_CONNECTED) return;

  ArduinoOTA.setHostname("ESP32S3-FootCtrl");
  ArduinoOTA.setPassword("Nexus8");

  ArduinoOTA.onStart([]() { Serial.println("OTA Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA End"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA Error[%u]\n", error); });

  ArduinoOTA.begin();
  Serial.println("OTA ready.");
}

// =====================
// Arduino Entry Points
// =====================
void setup() {
  Serial.begin(115200);
  delay(100);

  USB.manufacturerName("Egberts");
  USB.productName("ESP32-S3 Foot Controller");
  USB.serialNumber("FC-002");
  USB.begin();

  MIDI.begin();

  analogReadResolution(12);
  pinMode(POT_PIN, INPUT);

  rgb.begin();
  rgb.setBrightness(50);
  rgb.clear();
  rgb.show();

  prefs.begin("pedalcal", false);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(200000);
  u8g2.begin();

  showBootSplash();
  bootAnimation();

  loadCalibration();
  if (!validateCalibration()) {
    Serial.println("Calibration invalid; using defaults.");
    calibMin = 0;
    calibMax = 4095;
  }

  bool forceCal = shouldForceCalibrateAtBoot();

  connectWiFiWithTimeout();
  setupOTAIfConnected();

  calibratePedal(forceCal);

  Serial.printf("Calibration range: Min=%d Max=%d\n", calibMin, calibMax);
  Serial.println("ESP32-S3 USB MIDI Foot Controller ready.");
}

void loop() {
  maintainWiFi();
  if (WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();

  uint32_t now = millis();
  if (now - lastReadMs < READ_INTERVAL_MS) return;
  lastReadMs = now;

  int raw = analogRead(POT_PIN);

  static bool smoothInit = false;
  if (!smoothInit) {
    smoothRaw = (float)raw;
    smoothInit = true;
  } else {
    smoothRaw = smoothRaw + SMOOTH_ALPHA * ((float)raw - smoothRaw);
  }

  int mn = calibMin;
  int mx = calibMax;
  if (mn > mx) { int t = mn; mn = mx; mx = t; }
  if ((mx - mn) < CAL_MIN_SPAN) { mn = 0; mx = 4095; }

  int constrained = constrain((int)smoothRaw, mn, mx);
  uint8_t midiValue = (uint8_t)map(constrained, mn, mx, 0, 127);

  if (lastSentMidi == 255) lastSentMidi = midiValue;

  bool changedEnough = (abs((int)midiValue - (int)lastSentMidi) > MIDI_DEADBAND);
  bool rateOK = (now - lastMidiMs) >= MIDI_RATE_LIMIT_MS;

  if (changedEnough && rateOK) {
    MIDI.controlChange(CONTROLLER_NUM, midiValue, MIDI_CHANNEL);
    lastMidiMs = now;

    static uint32_t lastPrint = 0;
    if (now - lastPrint > 200) {
      Serial.printf("CC %u -> %u (Ch %u)\n", CONTROLLER_NUM, midiValue, MIDI_CHANNEL);
      lastPrint = now;
    }
    lastSentMidi = midiValue;
  }

  static uint8_t lastLedMidi = 255;
  if (lastLedMidi != midiValue) {
    setPedalColor(midiValue);
    lastLedMidi = midiValue;
  }

  updateDisplay(midiValue);
}
