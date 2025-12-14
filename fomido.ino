#include "USB.h"
#include "USBMIDI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <Wire.h>

// === Wi-Fi Credentials ===
const char* WIFI_SSID = "FritzseBadmuts";
const char* WIFI_PASS = "There is no sp00n!";

// === USB / MIDI ===
USBMIDI MIDI;

// === Hardware Configuration ===
const int POT_PIN = 4;
const int RGB_PIN = 48;
const int RGB_COUNT = 1;

// === OLED Configuration ===
#define I2C_SDA 10
#define I2C_SCL 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Full buffer mode - eliminates flicker/tearing on large filled areas
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// === MIDI Settings ===
const uint8_t MIDI_CHANNEL = 11;
const uint8_t CONTROLLER_NUM = 4;

// === Variables ===
uint8_t lastValue = 255;
float smoothRaw = 0;
int calibMin = 0;
int calibMax = 4095;
Adafruit_NeoPixel rgb(RGB_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

// === Pedal Bitmap (64x32, pixel-art wah/volume pedal icon) ===
// Converted to XBM format required by U8g2
#define pedalIcon64_width 64
#define pedalIcon64_height 32
static const unsigned char pedalIcon64_bits[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0x80,
  0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xC0,
  0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0x80,
  0x00,0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x03,0xFF,0xFF,0xFF,0xFF,0xFE,0x00,
  0x00,0x03,0xFF,0xFF,0xFF,0xFF,0xFE,0x00,
  0x00,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0x3F,0xFF,0xFF,0xFF,0xFF,0x00,
  0x00,0x00,0x1F,0xFF,0xFF,0xFF,0xFE,0x00,
  0x00,0x00,0x07,0xFF,0xFF,0xFF,0xFE,0x00,
  0x00,0xFF,0xC3,0xFF,0xFF,0xFF,0xFE,0x00,
  0x01,0xFF,0xE1,0xFF,0xFF,0xFF,0xFF,0x00,
  0x01,0xFF,0xFF,0x80,0x3F,0xFF,0xFF,0x00,
  0x01,0xFF,0xFF,0xC0,0x3F,0xFF,0xFF,0x80,
  0x01,0xFF,0xFF,0xC0,0x3F,0xFF,0xFF,0x80,
  0x01,0xFF,0xFF,0xC0,0x3F,0xFF,0xFF,0xC0,
  0x01,0xFF,0xFF,0xC0,0x3F,0xFF,0xFF,0xC0,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE0,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC0,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,
  0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80
};

// === Function Prototypes ===
void calibratePedal(bool force = false);
void setPedalColor(uint8_t midiValue);
void saveCalibration();
void loadCalibration();
void bootAnimation();
void updateDisplay(uint8_t midiValue, bool showCalibrating = false);
void showBootSplash();

// === Setup ===
void setup() {
  USB.manufacturerName("Egberts");
  USB.productName("ESP32-S3 Foot Controller");
  USB.serialNumber("FC-002");
  USB.begin();
  MIDI.begin();
  Serial.begin(115200);
  delay(200);
  analogReadResolution(12);
  pinMode(POT_PIN, INPUT);
  rgb.begin();
  rgb.setBrightness(50);
  rgb.show();
  prefs.begin("pedalcal", false);
  Wire.begin(I2C_SDA, I2C_SCL);
  // Lower I2C speed for stability (optional but recommended)
  Wire.setClock(100000);

  u8g2.begin();
  showBootSplash();
  bootAnimation();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  ArduinoOTA.setHostname("ESP32S3-FootCtrl");
  ArduinoOTA.setPassword("Nexus8");
  ArduinoOTA.begin();

  loadCalibration();
  calibratePedal(false);
  Serial.printf("Calibration range: Min=%d Max=%d\n", calibMin, calibMax);
  Serial.println("ESP32-S3 USB MIDI Foot Controller ready.");
}

// === Loop ===
void loop() {
  ArduinoOTA.handle();
  int raw = analogRead(POT_PIN);
  smoothRaw = (smoothRaw * 0.9f) + (raw * 0.1f);
  int constrained = constrain((int)smoothRaw, calibMin, calibMax);
  uint8_t midiValue = map(constrained, calibMin, calibMax, 0, 127);

  if (abs(midiValue - lastValue) >= 1) {
    MIDI.controlChange(CONTROLLER_NUM, midiValue, MIDI_CHANNEL);
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 200) {
      Serial.printf("CC %u -> %u (Ch %u)\n", CONTROLLER_NUM, midiValue, MIDI_CHANNEL);
      lastPrint = millis();
    }
    lastValue = midiValue;
  }

  setPedalColor(midiValue);
  updateDisplay(midiValue);
  delay(10);
}

// === Boot Splash ===
void showBootSplash() {
  u8g2.clearBuffer();
  u8g2.drawXBM((SCREEN_WIDTH - pedalIcon64_width) / 2, (SCREEN_HEIGHT - pedalIcon64_height) / 2 - 8,
               pedalIcon64_width, pedalIcon64_height, pedalIcon64_bits);
  u8g2.setFont(u8g2_font_helvR10_tr);  // Clean readable font
  u8g2.setDrawColor(1);
  int16_t textWidth = u8g2.getStrWidth("Foot Ctrl v1.0");
  u8g2.drawStr((SCREEN_WIDTH - textWidth) / 2, (SCREEN_HEIGHT - pedalIcon64_height) / 2 + 24, "Foot Ctrl v1.0");
  u8g2.sendBuffer();
  delay(1000);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// === Calibration ===
void calibratePedal(bool force) {
  bool hasData = prefs.isKey("min") && prefs.isKey("max");
  if (hasData && !force) {
    Serial.println("Using saved calibration.");
    return;
  }
  Serial.println("\n=== Pedal Calibration ===");
  Serial.println("Move pedal full range for 5 s...");
  calibMin = 4095;
  calibMax = 0;
  unsigned long start = millis();
  while (millis() - start < 5000) {
    int val = analogRead(POT_PIN);
    if (val < calibMin) calibMin = val;
    if (val > calibMax) calibMax = val;
    bool on = (millis() / 200) % 2;
    rgb.setPixelColor(0, on ? rgb.Color(0, 0, 255) : rgb.Color(0, 0, 0));
    rgb.show();
    updateDisplay(0, true);
    delay(10);
  }
  rgb.setPixelColor(0, rgb.Color(0, 255, 0));
  rgb.show();
  saveCalibration();
  Serial.printf("Calibration complete! Min: %d Max: %d\n", calibMin, calibMax);
  delay(1000);
  rgb.clear();
  rgb.show();
}

// === Save / Load Calibration ===
void saveCalibration() {
  prefs.putInt("min", calibMin);
  prefs.putInt("max", calibMax);
  Serial.println("Calibration saved to NVS.");
}

void loadCalibration() {
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

// === Boot Animation (RGB) ===
void bootAnimation() {
  for (int i = 0; i <= 127; i += 2) { setPedalColor(i); delay(5); }
  for (int i = 127; i >= 0; i -= 2) { setPedalColor(i); delay(5); }
  rgb.clear(); rgb.show();
}

// === RGB Gradient ===
void setPedalColor(uint8_t midiValue) {
  uint8_t red = 0, green = 0, blue = 0;
  if (midiValue < 64) {
    red = map(midiValue, 0, 63, 0, 128);
    blue = map(midiValue, 0, 63, 255, 128);
  } else {
    red = map(midiValue, 64, 127, 128, 255);
    blue = map(midiValue, 64, 127, 128, 0);
  }
  rgb.setPixelColor(0, rgb.Color(red, green, blue));
  rgb.show();
}

// === OLED Display Update ===
void updateDisplay(uint8_t midiValue, bool showCalibrating) {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 100) return;
  lastUpdate = millis();

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);  // White

  if (showCalibrating) {
    u8g2.setFont(u8g2_font_helvR12_tr);
    u8g2.drawStr(20, 25, "Calibrating...");
    u8g2.drawStr(10, 45, "Move pedal full range");
  } else {
    // Top line: CC and Channel
    u8g2.setFont(u8g2_font_6x12_tr);
    char top[20];
    sprintf(top, "CC%u Ch%u", CONTROLLER_NUM, MIDI_CHANNEL);
    u8g2.drawStr(0, 10, top);

    // Large MIDI value
    u8g2.setFont(u8g2_font_helvB24_tn);  // Big bold font
    char val[4];
    sprintf(val, "%3u", midiValue);
    int16_t w = u8g2.getStrWidth(val);
    u8g2.drawStr((SCREEN_WIDTH - w) / 2, 38, val);

    // Progress bar
    int barWidth = map(midiValue, 0, 127, 0, SCREEN_WIDTH - 10);
    u8g2.drawFrame(4, 44, SCREEN_WIDTH - 8, 10);        // Outline
    u8g2.drawBox(5, 45, barWidth, 8);                  // Filled part

    // Bottom status (IP or disconnected)
    u8g2.setFont(u8g2_font_6x12_tr);
    if (WiFi.status() == WL_CONNECTED) {
      String ip = WiFi.localIP().toString();
      u8g2.drawStr(0, 62, ip.c_str());
    } else {
      u8g2.drawStr(0, 62, "Wi-Fi not connected");
    }
  }

  u8g2.sendBuffer();
}
