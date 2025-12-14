#include "USB.h"
#include "USBMIDI.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
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
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
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
// === Pedal Bitmap (64x32, improved guitar effects foot pedal) ===
const unsigned char PROGMEM pedalIcon64[] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // Row 0
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 1
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 2
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // 3
  0x00,0x07,0xFF,0xFF,0xFF,0xFE,0x00,0x00,  // 4  Rounded top
  0x00,0x3F,0xFF,0xFF,0xFF,0xFF,0xE0,0x00,  // 5
  0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8,0x00,  // 6
  0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,0x00,  // 7  Main body
  0x07,0xFF,0xC0,0x00,0x00,0xFF,0xFF,0x00,  // 8  Footswitch circle starts (left)
  0x0F,0xFF,0x00,0x00,0x00,0x3F,0xFF,0x80,  // 9
  0x1F,0xFE,0x00,0x00,0x00,0x0F,0xFF,0xC0,  // 10
  0x1F,0xFC,0x00,0x00,0x00,0x07,0xFF,0xC0,  // 11 Circle center
  0x3F,0xFC,0x00,0x00,0x00,0x07,0xFF,0xE0,  // 12
  0x3F,0xF8,0x00,0x00,0x00,0x03,0xFF,0xE0,  // 13
  0x7F,0xF8,0x00,0x00,0x00,0x03,0xFF,0xF0,  // 14
  0x7F,0xF0,0x00,0x00,0x00,0x01,0xFF,0xF0,  // 15
  0x7F,0xF0,0x18,0x18,0x30,0x01,0xFF,0xF0,  // 16 Knobs start (small circles on right)
  0x7F,0xF0,0x38,0x1C,0x70,0x01,0xFF,0xF0,  // 17
  0x7F,0xF0,0x70,0x0E,0x70,0x01,0xFF,0xF0,  // 18
  0x7F,0xF0,0x70,0x0E,0x70,0x01,0xFF,0xF0,  // 19
  0x7F,0xF0,0x70,0x0E,0x70,0x01,0xFF,0xF0,  // 20
  0x7F,0xF0,0x38,0x1C,0x70,0x01,0xFF,0xF0,  // 21
  0x7F,0xF0,0x18,0x18,0x30,0x01,0xFF,0xF0,  // 22
  0xFF,0xF0,0x00,0x00,0x00,0x01,0xFF,0xF8,  // 23
  0xFF,0xF8,0x00,0x00,0x00,0x03,0xFF,0xF8,  // 24 Slanted treadle top
  0xFF,0xFF,0x00,0x00,0x00,0x0F,0xFF,0xFC,  // 25
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,  // 26 Full treadle
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,  // 27
  0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,  // 28 Bottom rounded
  0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,  // 29
  0x3F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,  // 30
  0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF8   // 31
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
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found!");
  } else {
    showBootSplash();
  }
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
// === Boot Splash (no fade) ===
void showBootSplash() {
  display.clearDisplay();
  display.drawBitmap((SCREEN_WIDTH - 64) / 2, (SCREEN_HEIGHT - 32) / 2 - 8,
                     pedalIcon64, 64, 32, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor((SCREEN_WIDTH - 72) / 2, (SCREEN_HEIGHT - 32) / 2 + 20);
  display.println("Foot Ctrl v1.0");
  display.display();
  delay(1000); // Show for 1 second
  display.clearDisplay();
  display.display();
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
  uint8_t red, green = 0, blue;
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
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (showCalibrating) {
    display.setTextSize(1);
    display.setCursor(20, 20);
    display.println("Calibrating...");
    display.setCursor(10, 40);
    display.println("Move pedal full range");
    display.display();
    return;
  }
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("CC%u Ch%u", CONTROLLER_NUM, MIDI_CHANNEL);
  display.setTextSize(2);
  display.setCursor(40, 15);
  display.printf("%3u", midiValue);
  int barWidth = map(midiValue, 0, 127, 0, SCREEN_WIDTH - 10);
  display.drawRect(0, 42, SCREEN_WIDTH - 10, 10, SSD1306_WHITE);
  display.fillRect(0, 42, barWidth, 10, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 55);
  if (WiFi.status() == WL_CONNECTED)
    display.printf("%s", WiFi.localIP().toString().c_str());
  else
    display.print("Wi-Fi not connected");
  display.display();
}
