#pragma once
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <Wire.h>
#include <Preferences.h>

// Manufacturer's ESP32-2432S032 demo + MCU V1.0 schematic; see docs/hardware.md.
constexpr int SCREEN_W = 320, SCREEN_H = 240;
constexpr uint8_t GFX_BL = 27;
constexpr uint8_t SD_CS = 5, SD_MOSI = 23, SD_CLK = 18, SD_MISO = 19;
SPIClass displaySpi(HSPI), sdSpi(VSPI);
// One SPIClass for LCD and resistive touch: transactions restore bus speed.
Arduino_DataBus *displayBus = new Arduino_HWSPI(2, 15, 14, 13, 12, &displaySpi, true);
Arduino_GFX *gfx = new Arduino_ST7789(displayBus, -1, 1, true, 240, 320);

void rotateTouch180(int &x, int &y) {
  x = SCREEN_W - 1 - x;
  y = SCREEN_H - 1 - y;
}

#if defined(CYD_TOUCH_RESISTIVE)
#include <XPT2046_Touchscreen.h>
constexpr const char *CYD_TOUCH_NAME = "XPT2046 resistief";
XPT2046_Touchscreen touch(33, 36);
int touchX0 = 4000, touchX1 = 100, touchY0 = 100, touchY1 = 4000;
bool readRawTouch(int &x, int &y) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint(); x = p.x; y = p.y; return p.z > 200;
}
bool readTouch(int &x, int &y) {
  int rx, ry;
  if (!readRawTouch(rx, ry)) return false;
  x = constrain(map(rx, touchX0, touchX1, 0, SCREEN_W - 1), 0L, long(SCREEN_W - 1));
  y = constrain(map(ry, touchY0, touchY1, 0, SCREEN_H - 1), 0L, long(SCREEN_H - 1));
  rotateTouch180(x, y);
  return true;
}
#elif defined(CYD_TOUCH_CAPACITIVE)
constexpr const char *CYD_TOUCH_NAME = "GT911 capacitief";
uint8_t gtAddress = 0;
bool gtRead(uint16_t reg, uint8_t *data, uint8_t n) {
  Wire.beginTransmission(gtAddress); Wire.write(reg >> 8); Wire.write(reg & 255);
  if (Wire.endTransmission() || Wire.requestFrom(gtAddress, n) != n) return false;
  for (int i = 0; i < n; ++i) data[i] = Wire.read();
  return true;
}
void gtAck() {
  Wire.beginTransmission(gtAddress); Wire.write(0x81); Wire.write(0x4e);
  Wire.write(0); Wire.endTransmission();
}
bool readTouch(int &x, int &y) {
  static bool down = false;
  static int lastX = 0, lastY = 0;
  static uint32_t lastPacket = 0;
  if (!gtAddress) return false;
  uint8_t status;
  if (!gtRead(0x814e, &status, 1)) return false;
  if (!(status & 0x80)) {
    x = lastX; y = lastY;
    return down && millis() - lastPacket < 150;
  }
  lastPacket = millis(); down = (status & 15) > 0 && (status & 15) <= 5;
  uint8_t p[7];
  if (down && gtRead(0x814f, p, 7)) {
    // Same ROTATION_RIGHT + inverted mapping as the vendor's 3.2" example.
    x = SCREEN_W - 1 - (p[3] | (p[4] << 8));
    y = p[1] | (p[2] << 8);
    down = x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H;
    if (down) rotateTouch180(x, y);
    lastX = x; lastY = y;
  } else down = false;
  gtAck(); return down;
}
#else
#error Select CYD_TOUCH_RESISTIVE or CYD_TOUCH_CAPACITIVE
#endif

void initBoard() {
  for (int pin : {4, 16, 17}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }
  pinMode(GFX_BL, OUTPUT); digitalWrite(GFX_BL, LOW);
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
#if defined(CYD_TOUCH_RESISTIVE)
  pinMode(33, OUTPUT); digitalWrite(33, HIGH);
#endif
  displaySpi.begin(14, 12, 13, 15);
  if (!gfx->begin(40000000)) Serial.println("Display initialisatie mislukt");
  gfx->fillScreen(0); digitalWrite(GFX_BL, HIGH);
#if defined(CYD_TOUCH_RESISTIVE)
  touch.begin(displaySpi); touch.setRotation(3);
  Preferences cal; cal.begin("cyd-touch", true);
  touchX0 = cal.getInt("x0", 4000); touchX1 = cal.getInt("x1", 100);
  touchY0 = cal.getInt("y0", 100); touchY1 = cal.getInt("y1", 4000);
  cal.end();
  if (abs(touchX1 - touchX0) < 500 || abs(touchY1 - touchY0) < 500) {
    touchX0 = 4000; touchX1 = 100; touchY0 = 100; touchY1 = 4000;
  }
#else
  Wire.begin(33, 32); Wire.setClock(100000); Wire.setTimeOut(30);
  for (uint8_t addr : {0x5d, 0x14}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) { gtAddress = addr; break; }
  }
  Serial.printf("GT911 adres: 0x%02x\n", gtAddress);
#endif
}
