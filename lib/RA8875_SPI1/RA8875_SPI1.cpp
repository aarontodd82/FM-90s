/*
 * RA8875_SPI1.cpp
 *
 * Simplified implementation of RA8875 driver with SPI1 support for Teensy 4.1
 * Based on Adafruit_RA8875 but modified to accept custom SPIClass
 */

#include "RA8875_SPI1.h"

// RA8875 registers
#define RA8875_PWRR             0x01  // Power and Display Control
#define RA8875_MRWC             0x02  // Memory Read/Write Command
#define RA8875_PCSR             0x04  // Pixel Clock Setting
#define RA8875_SYSR             0x10  // System Configuration
#define RA8875_HDWR             0x14  // LCD Horizontal Display Width
#define RA8875_HNDFTR           0x15  // Horizontal Non-Display Period Fine Tuning
#define RA8875_HNDR             0x16  // LCD Horizontal Non-Display Period
#define RA8875_HSTR             0x17  // HSYNC Start Position
#define RA8875_HPWR             0x18  // HSYNC Pulse Width
#define RA8875_VDHR0            0x19  // LCD Vertical Display Height Register 0
#define RA8875_VDHR1            0x1A  // LCD Vertical Display Height Register 1
#define RA8875_VNDR0            0x1B  // LCD Vertical Non-Display Period Register 0
#define RA8875_VNDR1            0x1C  // LCD Vertical Non-Display Period Register 1
#define RA8875_VSTR0            0x1D  // VSYNC Start Position Register 0
#define RA8875_VSTR1            0x1E  // VSYNC Start Position Register 1
#define RA8875_VPWR             0x1F  // VSYNC Pulse Width Register
#define RA8875_HSAW0            0x30  // Horizontal Start Point 0 of Active Window
#define RA8875_HSAW1            0x31  // Horizontal Start Point 1 of Active Window
#define RA8875_VSAW0            0x32  // Vertical Start Point 0 of Active Window
#define RA8875_VSAW1            0x33  // Vertical Start Point 1 of Active Window
#define RA8875_HEAW0            0x34  // Horizontal End Point 0 of Active Window
#define RA8875_HEAW1            0x35  // Horizontal End Point 1 of Active Window
#define RA8875_VEAW0            0x36  // Vertical End Point 0 of Active Window
#define RA8875_VEAW1            0x37  // Vertical End Point 1 of Active Window
#define RA8875_MWCR0            0x40  // Memory Write Control Register 0
#define RA8875_MWCR1            0x41  // Memory Write Control Register 1
#define RA8875_CURH0            0x46  // Memory Write Cursor Horizontal Position 0
#define RA8875_CURH1            0x47  // Memory Write Cursor Horizontal Position 1
#define RA8875_CURV0            0x48  // Memory Write Cursor Vertical Position 0
#define RA8875_CURV1            0x49  // Memory Write Cursor Vertical Position 1
#define RA8875_P1CR             0x8A  // PWM1 Control Register
#define RA8875_P1DCR            0x8B  // PWM1 Duty Cycle Register
#define RA8875_P2CR             0x8C  // PWM2 Control Register
#define RA8875_P2DCR            0x8D  // PWM2 Duty Cycle Register
#define RA8875_MCLR             0x8E  // Memory Clear Control Register
#define RA8875_DCR              0x90  // Draw Line/Circle/Square Control Register
#define RA8875_DLHSR0           0x91  // Draw Line/Square Horizontal Start Address Register 0
#define RA8875_DLHSR1           0x92  // Draw Line/Square Horizontal Start Address Register 1
#define RA8875_DLVSR0           0x93  // Draw Line/Square Vertical Start Address Register 0
#define RA8875_DLVSR1           0x94  // Draw Line/Square Vertical Start Address Register 1
#define RA8875_DLHER0           0x95  // Draw Line/Square Horizontal End Address Register 0
#define RA8875_DLHER1           0x96  // Draw Line/Square Horizontal End Address Register 1
#define RA8875_DLVER0           0x97  // Draw Line/Square Vertical End Address Register 0
#define RA8875_DLVER1           0x98  // Draw Line/Square Vertical End Address Register 1
#define RA8875_DCHR0            0x99  // Draw Circle Center Horizontal Address Register 0
#define RA8875_DCHR1            0x9A  // Draw Circle Center Horizontal Address Register 1
#define RA8875_DCVR0            0x9B  // Draw Circle Center Vertical Address Register 0
#define RA8875_DCVR1            0x9C  // Draw Circle Center Vertical Address Register 1
#define RA8875_DCRR             0x9D  // Draw Circle Radius Register
#define RA8875_ELLIPSE          0xA0  // Draw Ellipse/Circle Square Control Register
#define RA8875_ELL_A0           0xA1  // Draw Ellipse/Circle Square Long Axis Setting Register 0
#define RA8875_ELL_A1           0xA2  // Draw Ellipse/Circle Square Long Axis Setting Register 1
#define RA8875_ELL_B0           0xA3  // Draw Ellipse/Circle Square Short Axis Setting Register 0
#define RA8875_ELL_B1           0xA4  // Draw Ellipse/Circle Square Short Axis Setting Register 1
#define RA8875_DEHR0            0xA5  // Draw Ellipse/Circle Square Center Horizontal Address Register 0
#define RA8875_DEHR1            0xA6  // Draw Ellipse/Circle Square Center Horizontal Address Register 1
#define RA8875_DEVR0            0xA7  // Draw Ellipse/Circle Square Center Vertical Address Register 0
#define RA8875_DEVR1            0xA8  // Draw Ellipse/Circle Square Center Vertical Address Register 1
#define RA8875_FGCR0            0x63  // Foreground Color Register 0 (R)
#define RA8875_FGCR1            0x64  // Foreground Color Register 1 (G)
#define RA8875_FGCR2            0x65  // Foreground Color Register 2 (B)
#define RA8875_BGCR0            0x60  // Background Color Register 0 (R)
#define RA8875_BGCR1            0x61  // Background Color Register 1 (G)
#define RA8875_BGCR2            0x62  // Background Color Register 2 (B)
#define RA8875_FNCR0            0x21  // Font Control Register 0
#define RA8875_FNCR1            0x22  // Font Control Register 1
#define RA8875_F_CURXL          0x2A  // Font Write Cursor Horizontal Position Register 0
#define RA8875_F_CURXH          0x2B  // Font Write Cursor Horizontal Position Register 1
#define RA8875_F_CURYL          0x2C  // Font Write Cursor Vertical Position Register 0
#define RA8875_F_CURYH          0x2D  // Font Write Cursor Vertical Position Register 1
#define RA8875_GPIOX            0xC7  // Extra General Purpose IO Register

// SPI settings
// 2MHz - good balance between speed and reliability with breadboard wiring
#define RA8875_SPI_SPEED        2000000   // 2 MHz

RA8875_SPI1::RA8875_SPI1(uint8_t cs, uint8_t rst, SPIClass *spiInstance)
  : Adafruit_GFX(800, 480), _spi(spiInstance), _cs(cs), _rst(rst) {
  _width = 800;
  _height = 480;
  _textScale = 0;
}

boolean RA8875_SPI1::begin(enum RA8875sizes s) {
  Serial.println("[RA8875] begin() called");

  // Set CS and RST pins
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, HIGH);
  Serial.println("[RA8875] CS pin configured");

  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, LOW);
  delay(100);
  digitalWrite(_rst, HIGH);
  delay(200);  // Increased delay for RA8875 to fully reset
  Serial.println("[RA8875] Hardware reset complete");

  // NOTE: We DON'T call _spi->begin() here because the user should have
  // already called it AFTER configuring custom pins with setMOSI/setMISO/setSCK
  // If we call begin() here, it would reset the pin configuration!
  Serial.println("[RA8875] Skipping SPI begin (should be done in user code)");

  // Give chip a bit more time after reset
  delay(50);

  // Check if RA8875 is responding (read register 0x00 should return 0x75)
  // Do this BEFORE soft reset to verify communication works
  Serial.println("[RA8875] Reading register 0x00 to verify chip...");
  Serial.printf("[RA8875] Using SPI speed: %d Hz\n", RA8875_SPI_SPEED);

  uint8_t x = readReg(0x00);
  Serial.printf("[RA8875] Register 0x00 = 0x%02X (expected 0x75)\n", x);

  if (x != 0x75) {
    Serial.println("[RA8875] ERROR: Chip ID mismatch!");
    return false;  // Not found
  }

  Serial.println("[RA8875] Chip verified successfully");

  // NOTE: Skipping soft reset because hardware reset was already done
  // The soft reset was causing SPI communication to fail
  Serial.println("[RA8875] Skipping soft reset (hardware reset already done)");

  // Configure display based on size
  Serial.printf("[RA8875] Configuring for %s\n",
                s == RA8875_480x272 ? "480x272" : "800x480");

  // Configure PLL first (this is critical for stable display!)
  Serial.println("[RA8875] Configuring PLL...");

  if (s == RA8875_480x272) {
    _width = 480;
    _height = 272;

    // PLL Configuration for 480x272
    writeReg(0x88, 0x0B);  // PLLC1: PLL Control Register 1
    delay(1);
    writeReg(0x89, 0x02);  // PLLC2: PLL Control Register 2
    delay(1);

    // PCSR - Pixel Clock Setting Register
    writeReg(RA8875_PCSR, 0x82);  // PDAT on PCLK falling edge, PCLK = System Clock / 4

    // SYSR - System Configuration Register
    writeReg(RA8875_SYSR, 0x03);  // 16-bit color, 8-bit MPU interface

    // HDWR - LCD Horizontal Display Width Register
    writeReg(RA8875_HDWR, (_width / 8) - 1);

    // HNDFTR - Horizontal Non-Display Period Fine Tuning Option Register
    writeReg(RA8875_HNDFTR, 0x00);

    // HNDR - LCD Horizontal Non-Display Period Register
    writeReg(RA8875_HNDR, 0x03);

    // HSTR - HSYNC Start Position Register
    writeReg(RA8875_HSTR, 0x03);

    // HPWR - HSYNC Pulse Width Register
    writeReg(RA8875_HPWR, 0x0B);

    // VDHR0 - LCD Vertical Display Height Register 0
    writeReg(RA8875_VDHR0, (uint16_t)(_height - 1) & 0xFF);

    // VDHR1 - LCD Vertical Display Height Register 1
    writeReg(RA8875_VDHR1, (uint16_t)(_height - 1) >> 8);

    // VNDR0 - LCD Vertical Non-Display Period Register 0
    writeReg(RA8875_VNDR0, 0x0F);

    // VNDR1 - LCD Vertical Non-Display Period Register 1
    writeReg(RA8875_VNDR1, 0x00);

    // VSTR0 - VSYNC Start Position Register 0
    writeReg(RA8875_VSTR0, 0x02);

    // VSTR1 - VSYNC Start Position Register 1
    writeReg(RA8875_VSTR1, 0x00);

    // VPWR - VSYNC Pulse Width Register
    writeReg(RA8875_VPWR, 0x09);

  } else { // RA8875_800x480
    _width = 800;
    _height = 480;

    // PLL Configuration for 800x480
    // This sets up the system clock for proper pixel timing
    Serial.println("[RA8875]   Setting PLL registers...");
    writeReg(0x88, 0x0C);  // PLLC1: PLL Control Register 1 (multiplier)
    delay(1);
    writeReg(0x89, 0x02);  // PLLC2: PLL Control Register 2 (divider)
    delay(10);  // Wait for PLL to stabilize
    Serial.println("[RA8875]   PLL configured and stabilized");

    // PCSR - Pixel Clock Setting Register
    writeReg(RA8875_PCSR, 0x82);  // PDAT on PCLK falling edge, PCLK = System Clock / 4
                                   // Changed from /2 to /4 for more stable timing

    // SYSR - System Configuration Register
    // Bits 3-2: 11 = 65K colors (16-bit), must set BOTH bits per RA8875 bug workaround
    // Bits 1-0: 00 = 8-bit MCU interface
    writeReg(RA8875_SYSR, 0x0C);  // 16-bit color (65K), 8-bit MPU interface

    // HDWR - LCD Horizontal Display Width Register
    writeReg(RA8875_HDWR, (_width / 8) - 1);

    // HNDFTR - Horizontal Non-Display Period Fine Tuning Option Register
    writeReg(RA8875_HNDFTR, 0x00);

    // HNDR - LCD Horizontal Non-Display Period Register
    writeReg(RA8875_HNDR, 0x03);

    // HSTR - HSYNC Start Position Register
    writeReg(RA8875_HSTR, 0x03);

    // HPWR - HSYNC Pulse Width Register
    writeReg(RA8875_HPWR, 0x0B);

    // VDHR0 - LCD Vertical Display Height Register 0
    writeReg(RA8875_VDHR0, (uint16_t)(_height - 1) & 0xFF);

    // VDHR1 - LCD Vertical Display Height Register 1
    writeReg(RA8875_VDHR1, (uint16_t)(_height - 1) >> 8);

    // VNDR0 - LCD Vertical Non-Display Period Register 0
    writeReg(RA8875_VNDR0, 0x1F);

    // VNDR1 - LCD Vertical Non-Display Period Register 1
    writeReg(RA8875_VNDR1, 0x00);

    // VSTR0 - VSYNC Start Position Register 0
    writeReg(RA8875_VSTR0, 0x07);

    // VSTR1 - VSYNC Start Position Register 1
    writeReg(RA8875_VSTR1, 0x00);

    // VPWR - VSYNC Pulse Width Register
    writeReg(RA8875_VPWR, 0x09);
  }

  // Set active window to full screen
  writeReg(RA8875_HSAW0, 0x00);
  writeReg(RA8875_HSAW1, 0x00);
  writeReg(RA8875_HEAW0, (uint16_t)(_width - 1) & 0xFF);
  writeReg(RA8875_HEAW1, (uint16_t)(_width - 1) >> 8);

  writeReg(RA8875_VSAW0, 0x00);
  writeReg(RA8875_VSAW1, 0x00);
  writeReg(RA8875_VEAW0, (uint16_t)(_height - 1) & 0xFF);
  writeReg(RA8875_VEAW1, (uint16_t)(_height - 1) >> 8);

  // Turn on display
  Serial.println("[RA8875] Setting active window and turning on display...");
  writeReg(RA8875_PWRR, 0x80);
  delay(1);

  Serial.println("[RA8875] Initialization complete - SUCCESS!");
  return true;
}

void RA8875_SPI1::softReset(void) {
  writeCommand(RA8875_PWRR);
  writeData(0x01);
  delay(1);
  writeData(0x00);
  delay(100);
}

void RA8875_SPI1::displayOn(boolean on) {
  if (on) {
    writeReg(RA8875_PWRR, 0x80);
  } else {
    writeReg(RA8875_PWRR, 0x00);
  }
}

void RA8875_SPI1::sleep(boolean sleep) {
  if (sleep) {
    writeReg(RA8875_PWRR, 0x02);
  } else {
    writeReg(RA8875_PWRR, 0x80);
  }
}

void RA8875_SPI1::fillScreen(uint16_t color) {
  graphicsMode();
  fillRect(0, 0, _width, _height, color);
}

void RA8875_SPI1::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height)) return;

  graphicsMode();

  // Set cursor
  writeReg(RA8875_CURH0, x & 0xFF);
  writeReg(RA8875_CURH1, x >> 8);
  writeReg(RA8875_CURV0, y & 0xFF);
  writeReg(RA8875_CURV1, y >> 8);

  // Write pixel
  writeCommand(RA8875_MRWC);
  spiBegin();
  spiTransfer(0x00);  // Data write
  spiTransfer(color >> 8);
  spiTransfer(color & 0xFF);
  spiEnd();
}

void RA8875_SPI1::drawPixels(uint16_t *p, uint32_t count, int16_t x, int16_t y) {
  if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height) || count == 0) return;

  graphicsMode();

  // Set cursor position once
  writeReg(RA8875_CURH0, x & 0xFF);
  writeReg(RA8875_CURH1, x >> 8);
  writeReg(RA8875_CURV0, y & 0xFF);
  writeReg(RA8875_CURV1, y >> 8);

  // Start memory write command
  writeCommand(RA8875_MRWC);

  // Stream all pixels in one SPI transaction
  spiBegin();
  spiTransfer(0x00);  // Data write mode
  while (count--) {
    spiTransfer(*p >> 8);
    spiTransfer(*p & 0xFF);
    p++;
  }
  spiEnd();
}

void RA8875_SPI1::drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data) {
  if ((x < 0) || (y < 0) || (x + w > _width) || (y + h > _height) || w <= 0 || h <= 0) return;

  graphicsMode();

  // Set active window to image bounds (enables auto-wrap at image width)
  writeReg(RA8875_HSAW0, x & 0xFF);
  writeReg(RA8875_HSAW1, x >> 8);
  writeReg(RA8875_HEAW0, (x + w - 1) & 0xFF);
  writeReg(RA8875_HEAW1, (x + w - 1) >> 8);
  writeReg(RA8875_VSAW0, y & 0xFF);
  writeReg(RA8875_VSAW1, y >> 8);
  writeReg(RA8875_VEAW0, (y + h - 1) & 0xFF);
  writeReg(RA8875_VEAW1, (y + h - 1) >> 8);

  // Set cursor to top-left of image
  writeReg(RA8875_CURH0, x & 0xFF);
  writeReg(RA8875_CURH1, x >> 8);
  writeReg(RA8875_CURV0, y & 0xFF);
  writeReg(RA8875_CURV1, y >> 8);

  // Start memory write command
  writeCommand(RA8875_MRWC);

  // Stream entire image in one SPI transaction
  uint32_t count = (uint32_t)w * h;
  spiBegin();
  spiTransfer(0x00);  // Data write mode
  while (count--) {
    spiTransfer(*data >> 8);
    spiTransfer(*data & 0xFF);
    data++;
  }
  spiEnd();

  // Restore active window to full screen
  writeReg(RA8875_HSAW0, 0x00);
  writeReg(RA8875_HSAW1, 0x00);
  writeReg(RA8875_HEAW0, (_width - 1) & 0xFF);
  writeReg(RA8875_HEAW1, (_width - 1) >> 8);
  writeReg(RA8875_VSAW0, 0x00);
  writeReg(RA8875_VSAW1, 0x00);
  writeReg(RA8875_VEAW0, (_height - 1) & 0xFF);
  writeReg(RA8875_VEAW1, (_height - 1) >> 8);
}

void RA8875_SPI1::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set line coordinates
  writeReg(RA8875_DLHSR0, x0 & 0xFF);
  writeReg(RA8875_DLHSR1, x0 >> 8);
  writeReg(RA8875_DLVSR0, y0 & 0xFF);
  writeReg(RA8875_DLVSR1, y0 >> 8);
  writeReg(RA8875_DLHER0, x1 & 0xFF);
  writeReg(RA8875_DLHER1, x1 >> 8);
  writeReg(RA8875_DLVER0, y1 & 0xFF);
  writeReg(RA8875_DLVER1, y1 >> 8);

  // Draw line
  writeReg(RA8875_DCR, 0x80);
  waitPoll(RA8875_DCR, 0x80);
}

void RA8875_SPI1::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set rectangle coordinates
  writeReg(RA8875_DLHSR0, x & 0xFF);
  writeReg(RA8875_DLHSR1, x >> 8);
  writeReg(RA8875_DLVSR0, y & 0xFF);
  writeReg(RA8875_DLVSR1, y >> 8);
  writeReg(RA8875_DLHER0, (x + w - 1) & 0xFF);
  writeReg(RA8875_DLHER1, (x + w - 1) >> 8);
  writeReg(RA8875_DLVER0, (y + h - 1) & 0xFF);
  writeReg(RA8875_DLVER1, (y + h - 1) >> 8);

  // Draw rectangle (unfilled)
  writeReg(RA8875_DCR, 0x90);
  waitPoll(RA8875_DCR, 0x80);
}

void RA8875_SPI1::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set rectangle coordinates
  writeReg(RA8875_DLHSR0, x & 0xFF);
  writeReg(RA8875_DLHSR1, x >> 8);
  writeReg(RA8875_DLVSR0, y & 0xFF);
  writeReg(RA8875_DLVSR1, y >> 8);
  writeReg(RA8875_DLHER0, (x + w - 1) & 0xFF);
  writeReg(RA8875_DLHER1, (x + w - 1) >> 8);
  writeReg(RA8875_DLVER0, (y + h - 1) & 0xFF);
  writeReg(RA8875_DLVER1, (y + h - 1) >> 8);

  // Draw filled rectangle
  writeReg(RA8875_DCR, 0xB0);
  waitPoll(RA8875_DCR, 0x80);
}

void RA8875_SPI1::drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set circle center and radius
  writeReg(RA8875_DCHR0, x & 0xFF);
  writeReg(RA8875_DCHR1, x >> 8);
  writeReg(RA8875_DCVR0, y & 0xFF);
  writeReg(RA8875_DCVR1, y >> 8);
  writeReg(RA8875_DCRR, r);

  // Draw circle (unfilled)
  writeReg(RA8875_DCR, 0x40);
  waitPoll(RA8875_DCR, 0x40);
}

void RA8875_SPI1::fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set circle center and radius
  writeReg(RA8875_DCHR0, x & 0xFF);
  writeReg(RA8875_DCHR1, x >> 8);
  writeReg(RA8875_DCVR0, y & 0xFF);
  writeReg(RA8875_DCVR1, y >> 8);
  writeReg(RA8875_DCRR, r);

  // Draw filled circle
  writeReg(RA8875_DCR, 0x60);
  waitPoll(RA8875_DCR, 0x40);
}

void RA8875_SPI1::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  drawLine(x, y, x, y + h - 1, color);
}

void RA8875_SPI1::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  drawLine(x, y, x + w - 1, y, color);
}

void RA8875_SPI1::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  drawLine(x0, y0, x1, y1, color);
  drawLine(x1, y1, x2, y2, color);
  drawLine(x2, y2, x0, y0, color);
}

void RA8875_SPI1::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  // RA8875 doesn't have hardware triangle support, use software implementation
  Adafruit_GFX::fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

void RA8875_SPI1::drawEllipse(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set ellipse parameters
  writeReg(RA8875_DEHR0, xCenter & 0xFF);
  writeReg(RA8875_DEHR1, xCenter >> 8);
  writeReg(RA8875_DEVR0, yCenter & 0xFF);
  writeReg(RA8875_DEVR1, yCenter >> 8);
  writeReg(RA8875_ELL_A0, longAxis & 0xFF);
  writeReg(RA8875_ELL_A1, longAxis >> 8);
  writeReg(RA8875_ELL_B0, shortAxis & 0xFF);
  writeReg(RA8875_ELL_B1, shortAxis >> 8);

  // Draw ellipse
  writeReg(RA8875_ELLIPSE, 0x80);
  waitPoll(RA8875_ELLIPSE, 0x80);
}

void RA8875_SPI1::fillEllipse(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set ellipse parameters
  writeReg(RA8875_DEHR0, xCenter & 0xFF);
  writeReg(RA8875_DEHR1, xCenter >> 8);
  writeReg(RA8875_DEVR0, yCenter & 0xFF);
  writeReg(RA8875_DEVR1, yCenter >> 8);
  writeReg(RA8875_ELL_A0, longAxis & 0xFF);
  writeReg(RA8875_ELL_A1, longAxis >> 8);
  writeReg(RA8875_ELL_B0, shortAxis & 0xFF);
  writeReg(RA8875_ELL_B1, shortAxis >> 8);

  // Draw filled ellipse
  writeReg(RA8875_ELLIPSE, 0xC0);
  waitPoll(RA8875_ELLIPSE, 0x80);
}

void RA8875_SPI1::drawCurve(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint8_t curvePart, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set curve parameters
  writeReg(RA8875_DEHR0, xCenter & 0xFF);
  writeReg(RA8875_DEHR1, xCenter >> 8);
  writeReg(RA8875_DEVR0, yCenter & 0xFF);
  writeReg(RA8875_DEVR1, yCenter >> 8);
  writeReg(RA8875_ELL_A0, longAxis & 0xFF);
  writeReg(RA8875_ELL_A1, longAxis >> 8);
  writeReg(RA8875_ELL_B0, shortAxis & 0xFF);
  writeReg(RA8875_ELL_B1, shortAxis >> 8);

  // Draw curve
  writeReg(RA8875_ELLIPSE, 0x90 | (curvePart & 0x03));
  waitPoll(RA8875_ELLIPSE, 0x80);
}

void RA8875_SPI1::fillCurve(int16_t xCenter, int16_t yCenter, int16_t longAxis, int16_t shortAxis, uint8_t curvePart, uint16_t color) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (color & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (color & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (color & 0x001F));

  // Set curve parameters
  writeReg(RA8875_DEHR0, xCenter & 0xFF);
  writeReg(RA8875_DEHR1, xCenter >> 8);
  writeReg(RA8875_DEVR0, yCenter & 0xFF);
  writeReg(RA8875_DEVR1, yCenter >> 8);
  writeReg(RA8875_ELL_A0, longAxis & 0xFF);
  writeReg(RA8875_ELL_A1, longAxis >> 8);
  writeReg(RA8875_ELL_B0, shortAxis & 0xFF);
  writeReg(RA8875_ELL_B1, shortAxis >> 8);

  // Draw filled curve
  writeReg(RA8875_ELLIPSE, 0xD0 | (curvePart & 0x03));
  waitPoll(RA8875_ELLIPSE, 0x80);
}

void RA8875_SPI1::textMode(void) {
  writeCommand(RA8875_MWCR0);
  uint8_t temp = readData();
  temp |= 0x80;  // Set text mode
  writeData(temp);

  // Set internal font
  writeReg(RA8875_FNCR0, 0x00);
}

void RA8875_SPI1::textSetCursor(uint16_t x, uint16_t y) {
  writeReg(RA8875_F_CURXL, x & 0xFF);
  writeReg(RA8875_F_CURXH, x >> 8);
  writeReg(RA8875_F_CURYL, y & 0xFF);
  writeReg(RA8875_F_CURYH, y >> 8);
}

void RA8875_SPI1::textColor(uint16_t foreColor, uint16_t bgColor) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (foreColor & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (foreColor & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (foreColor & 0x001F));

  // Set background color
  writeReg(RA8875_BGCR0, (bgColor & 0xF800) >> 11);
  writeReg(RA8875_BGCR1, (bgColor & 0x07E0) >> 5);
  writeReg(RA8875_BGCR2, (bgColor & 0x001F));

  // Disable transparent background
  writeReg(RA8875_FNCR1, 0x00);
}

void RA8875_SPI1::textTransparent(uint16_t foreColor) {
  // Set foreground color
  writeReg(RA8875_FGCR0, (foreColor & 0xF800) >> 11);
  writeReg(RA8875_FGCR1, (foreColor & 0x07E0) >> 5);
  writeReg(RA8875_FGCR2, (foreColor & 0x001F));

  // Enable transparent background
  writeReg(RA8875_FNCR1, 0x40);
}

void RA8875_SPI1::textEnlarge(uint8_t scale) {
  if (scale > 3) scale = 3;  // Max scale is 4x (value 3)

  _textScale = scale;

  // Set horizontal and vertical scaling
  uint8_t temp = (scale << 2) | scale;
  writeReg(RA8875_FNCR1, temp);
}

void RA8875_SPI1::textWrite(const char *buffer, uint16_t len) {
  if (len == 0) len = strlen(buffer);

  writeCommand(RA8875_MRWC);

  for (uint16_t i = 0; i < len; i++) {
    writeData(buffer[i]);
    waitBusy(0x80);
  }
}

void RA8875_SPI1::graphicsMode(void) {
  writeCommand(RA8875_MWCR0);
  uint8_t temp = readData();
  temp &= ~0x80;  // Clear text mode bit
  writeData(temp);
}

void RA8875_SPI1::PWM1config(boolean on, uint8_t clock) {
  if (on) {
    writeReg(RA8875_P1CR, 0x80 | (clock & 0x0F));
  } else {
    writeReg(RA8875_P1CR, 0x00);
  }
}

void RA8875_SPI1::PWM2config(boolean on, uint8_t clock) {
  if (on) {
    writeReg(RA8875_P2CR, 0x80 | (clock & 0x0F));
  } else {
    writeReg(RA8875_P2CR, 0x00);
  }
}

void RA8875_SPI1::PWM1out(uint8_t p) {
  writeReg(RA8875_P1DCR, p);
}

void RA8875_SPI1::PWM2out(uint8_t p) {
  writeReg(RA8875_P2DCR, p);
}

void RA8875_SPI1::GPIOX(boolean on) {
  if (on) {
    writeReg(RA8875_GPIOX, 0x01);
  } else {
    writeReg(RA8875_GPIOX, 0x00);
  }
}

void RA8875_SPI1::writeReg(uint8_t reg, uint8_t val) {
  writeCommand(reg);
  writeData(val);
}

uint8_t RA8875_SPI1::readReg(uint8_t reg) {
  writeCommand(reg);
  return readData();
}

void RA8875_SPI1::writeData(uint8_t d) {
  spiBegin();
  spiTransfer(0x00);  // Data write mode
  spiTransfer(d);
  spiEnd();
}

uint8_t RA8875_SPI1::readData(void) {
  spiBegin();
  spiTransfer(0x40);  // Data read mode
  uint8_t x = spiTransfer(0x00);  // Dummy read
  spiEnd();
  return x;
}

void RA8875_SPI1::writeCommand(uint8_t d) {
  spiBegin();
  spiTransfer(0x80);  // Command write mode
  spiTransfer(d);
  spiEnd();
}

uint8_t RA8875_SPI1::readStatus(void) {
  spiBegin();
  spiTransfer(0xC0);  // Status read mode
  uint8_t x = spiTransfer(0x00);
  spiEnd();
  return x;
}

void RA8875_SPI1::waitPoll(uint8_t r, uint8_t f) {
  while (1) {
    uint8_t temp = readReg(r);
    if (!(temp & f)) break;
  }
}

void RA8875_SPI1::waitBusy(uint8_t res) {
  uint8_t temp;
  unsigned long start = millis();

  do {
    if (millis() - start > 10) return;  // Timeout after 10ms
    temp = readStatus();
  } while ((temp & res) == res);
}

void RA8875_SPI1::spiBegin() {
  _spi->beginTransaction(SPISettings(RA8875_SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs, LOW);
}

void RA8875_SPI1::spiEnd() {
  digitalWrite(_cs, HIGH);
  _spi->endTransaction();
}

uint8_t RA8875_SPI1::spiTransfer(uint8_t data) {
  return _spi->transfer(data);
}
