/*
 * RA8875_SPI1.h
 *
 * Extended Adafruit_RA8875 class that supports using SPI1 instead of default SPI
 * Specifically designed for Teensy 4.1 with custom SPI pins
 *
 * This wrapper allows you to specify which SPIClass to use (SPI, SPI1, SPI2)
 * when initializing the RA8875 display controller.
 */

#ifndef _RA8875_SPI1_H
#define _RA8875_SPI1_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>

// RA8875 display sizes
enum RA8875sizes {
  RA8875_480x272,
  RA8875_800x480
};

// Color definitions (RGB565 format)
#define RA8875_BLACK            0x0000
#define RA8875_BLUE             0x001F
#define RA8875_RED              0xF800
#define RA8875_GREEN            0x07E0
#define RA8875_CYAN             0x07FF
#define RA8875_MAGENTA          0xF81F
#define RA8875_YELLOW           0xFFE0
#define RA8875_WHITE            0xFFFF

// Simplified RA8875 class that uses a custom SPIClass
class RA8875_SPI1 : public Adafruit_GFX {
public:
  RA8875_SPI1(uint8_t cs, uint8_t rst, SPIClass *spiInstance = &SPI);

  boolean begin(enum RA8875sizes s);
  void softReset(void);
  void displayOn(boolean on);
  void sleep(boolean sleep);

  // Screen management
  void fillScreen(uint16_t color);
  void drawPixel(int16_t x, int16_t y, uint16_t color);
  void drawPixels(uint16_t *p, uint32_t count, int16_t x, int16_t y);
  void drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data);
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

  // Shape drawing
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
  void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
  void drawEllipse(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint16_t color);
  void fillEllipse(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint16_t color);
  void drawCurve(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint8_t curvePart, uint16_t color);
  void fillCurve(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint8_t curvePart, uint16_t color);

  // Text functions
  void textMode(void);
  void textSetCursor(uint16_t x, uint16_t y);
  void textColor(uint16_t foreColor, uint16_t bgColor);
  void textTransparent(uint16_t foreColor);
  void textEnlarge(uint8_t scale);
  void textWrite(const char *buffer, uint16_t len = 0);

  // Graphics mode
  void graphicsMode(void);

  // PWM and brightness control
  void PWM1config(boolean on, uint8_t clock);
  void PWM2config(boolean on, uint8_t clock);
  void PWM1out(uint8_t p);
  void PWM2out(uint8_t p);

  // GPIO
  void GPIOX(boolean on);

  // Low-level register access
  void writeReg(uint8_t reg, uint8_t val);
  uint8_t readReg(uint8_t reg);
  void writeData(uint8_t d);
  uint8_t readData(void);
  void writeCommand(uint8_t d);
  uint8_t readStatus(void);

private:
  SPIClass *_spi;
  uint8_t _cs, _rst;
  uint16_t _width, _height;
  uint8_t _textScale;

  void waitPoll(uint8_t r, uint8_t f);
  void waitBusy(uint8_t res);
  void writeRegData(uint8_t reg, uint8_t val);

  // SPI transaction helpers
  void spiBegin();
  void spiEnd();
  uint8_t spiTransfer(uint8_t data);
};

// PWM clock values
#define RA8875_PWM_CLK_DIV1     0x00
#define RA8875_PWM_CLK_DIV2     0x01
#define RA8875_PWM_CLK_DIV4     0x02
#define RA8875_PWM_CLK_DIV8     0x03
#define RA8875_PWM_CLK_DIV16    0x04
#define RA8875_PWM_CLK_DIV32    0x05
#define RA8875_PWM_CLK_DIV64    0x06
#define RA8875_PWM_CLK_DIV128   0x07
#define RA8875_PWM_CLK_DIV256   0x08
#define RA8875_PWM_CLK_DIV512   0x09
#define RA8875_PWM_CLK_DIV1024  0x0A
#define RA8875_PWM_CLK_DIV2048  0x0B
#define RA8875_PWM_CLK_DIV4096  0x0C
#define RA8875_PWM_CLK_DIV8192  0x0D
#define RA8875_PWM_CLK_DIV16384 0x0E
#define RA8875_PWM_CLK_DIV32768 0x0F

#endif // _RA8875_SPI1_H
