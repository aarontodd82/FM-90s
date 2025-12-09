#ifndef RETRO_UI_H
#define RETRO_UI_H

#include <Arduino.h>
#include "RA8875_SPI1.h"
#include "dos_colors.h"

// RetroUI - DOS-style text-based UI system for RA8875 display
// Implements a character grid system (100x30 @ 8x16 pixels per char)
class RetroUI {
private:
    RA8875_SPI1* tft;

    // Character grid dimensions
    static const uint8_t CHAR_WIDTH = 8;
    static const uint8_t CHAR_HEIGHT = 16;
    static const uint8_t GRID_COLS = 100;  // 800 / 8
    static const uint8_t GRID_ROWS = 30;   // 480 / 16

    // Current text colors (for text mode operations)
    uint16_t currentFgColor;
    uint16_t currentBgColor;

    // Status notification system (temporary messages on bottom-right)
    String statusNotificationMessage;
    uint32_t statusNotificationExpireTime;
    uint16_t statusNotificationColor;
    uint16_t statusNotificationFgColor;
    bool statusNotificationActive;

    // Convert grid coordinates to pixel coordinates
    void gridToPixel(uint8_t col, uint8_t row, int16_t* x, int16_t* y) {
        *x = col * CHAR_WIDTH;
        *y = row * CHAR_HEIGHT;
    }

public:
    RetroUI(RA8875_SPI1* display) : tft(display),
                                     currentFgColor(DOS_WHITE),
                                     currentBgColor(DOS_BLUE),
                                     statusNotificationExpireTime(0),
                                     statusNotificationColor(DOS_BLACK),
                                     statusNotificationFgColor(DOS_BLACK),
                                     statusNotificationActive(false) {
        // Ensure we're in graphics mode for drawing operations
        tft->graphicsMode();
    }

    // Get the underlying TFT display (for direct pixel operations like images)
    RA8875_SPI1* getTFT() { return tft; }

    // Clear the entire screen with a color
    void clear(uint16_t color = DOS_BLUE) {
        tft->fillScreen(color);
        currentBgColor = color;
    }

    // Fill a rectangle in grid coordinates with a color
    void fillGridRect(uint8_t col, uint8_t row, uint8_t width, uint8_t height, uint16_t color) {
        if (col >= GRID_COLS || row >= GRID_ROWS) return;

        // Clamp to grid boundaries
        if (col + width > GRID_COLS) width = GRID_COLS - col;
        if (row + height > GRID_ROWS) height = GRID_ROWS - row;

        int16_t x, y;
        gridToPixel(col, row, &x, &y);
        tft->fillRect(x, y, width * CHAR_WIDTH, height * CHAR_HEIGHT, color);
    }

    // Draw text at grid position with specified colors
    void drawText(uint8_t col, uint8_t row, const char* text,
                  uint16_t fgColor = DOS_WHITE, uint16_t bgColor = DOS_BLUE) {
        if (!text || col >= GRID_COLS || row >= GRID_ROWS) return;

        int16_t x, y;
        gridToPixel(col, row, &x, &y);

        // Calculate text length (clamped to screen width)
        uint8_t textLen = strlen(text);
        if (col + textLen > GRID_COLS) {
            textLen = GRID_COLS - col;
        }

        // Fill background for text area
        fillGridRect(col, row, textLen, 1, bgColor);

        // Draw the text
        tft->textMode();
        tft->textSetCursor(x, y);
        tft->textColor(fgColor, bgColor);
        tft->textEnlarge(0);  // Normal size (8x16)

        // Write text, truncating if necessary
        if (textLen < strlen(text)) {
            char truncated[GRID_COLS + 1];
            strncpy(truncated, text, textLen);
            truncated[textLen] = '\0';
            tft->textWrite(truncated);
        } else {
            tft->textWrite(text);
        }

        tft->graphicsMode();
    }

    // Draw a window with double-line border (MS-DOS style)
    void drawWindow(uint8_t col, uint8_t row, uint8_t width, uint8_t height,
                    const char* title = nullptr,
                    uint16_t fgColor = DOS_WHITE, uint16_t bgColor = DOS_BLUE) {
        if (col >= GRID_COLS || row >= GRID_ROWS || width < 3 || height < 3) return;

        // Fill background
        fillGridRect(col, row, width, height, bgColor);

        // Draw border using graphics primitives to simulate DOS double-line box
        int16_t x, y;
        gridToPixel(col, row, &x, &y);
        int16_t pixelW = width * CHAR_WIDTH;
        int16_t pixelH = height * CHAR_HEIGHT;

        // Double border effect (2 pixels apart for MS-DOS look)
        tft->drawRect(x, y, pixelW, pixelH, fgColor);
        tft->drawRect(x + 2, y + 2, pixelW - 4, pixelH - 4, fgColor);

        // Horizontal separator line at row 2 (for title bar separation)
        if (height > 3) {
            tft->drawFastHLine(x, y + (2 * CHAR_HEIGHT), pixelW, fgColor);
            tft->drawFastHLine(x + 2, y + (2 * CHAR_HEIGHT) + 2, pixelW - 4, fgColor);
        }

        // Draw title if provided
        if (title && strlen(title) > 0) {
            // Center the title
            uint8_t titleLen = strlen(title);
            uint8_t titleCol = col + 1;
            if (titleLen < width - 2) {
                titleCol = col + (width - titleLen) / 2;
            }

            drawText(titleCol, row, title, fgColor, bgColor);
        }
    }

    // Draw a panel with single-line border
    void drawPanel(uint8_t col, uint8_t row, uint8_t width, uint8_t height,
                   const char* title = nullptr,
                   uint16_t fgColor = DOS_WHITE, uint16_t bgColor = DOS_BLUE) {
        if (col >= GRID_COLS || row >= GRID_ROWS || width < 3 || height < 3) return;

        // Fill background
        fillGridRect(col, row, width, height, bgColor);

        // Draw single border
        int16_t x, y;
        gridToPixel(col, row, &x, &y);
        int16_t pixelW = width * CHAR_WIDTH;
        int16_t pixelH = height * CHAR_HEIGHT;

        tft->drawRect(x, y, pixelW, pixelH, fgColor);

        // Draw title if provided
        if (title && strlen(title) > 0) {
            drawText(col + 1, row, title, fgColor, bgColor);
        }
    }

    // Draw a progress bar
    void drawProgressBar(uint8_t col, uint8_t row, uint8_t width,
                        float percentage,  // 0.0 to 1.0
                        uint16_t fgColor = DOS_BRIGHT_CYAN,
                        uint16_t bgColor = DOS_BLUE) {
        if (col >= GRID_COLS || row >= GRID_ROWS || width < 3) return;

        // Clamp percentage
        if (percentage < 0.0f) percentage = 0.0f;
        if (percentage > 1.0f) percentage = 1.0f;

        // Calculate dimensions
        uint8_t innerWidth = width - 2;
        uint8_t filledWidth = (uint8_t)(innerWidth * percentage);

        int16_t x, y;
        gridToPixel(col, row, &x, &y);

        // Draw bracket characters
        drawText(col, row, "[", fgColor, bgColor);
        drawText(col + width - 1, row, "]", fgColor, bgColor);

        // Use filled rectangles for DOS-style block appearance
        // Filled portion - solid blocks
        if (filledWidth > 0) {
            int16_t fillX = x + CHAR_WIDTH;
            int16_t fillWidth = filledWidth * CHAR_WIDTH;
            tft->fillRect(fillX, y + 2, fillWidth, CHAR_HEIGHT - 4, fgColor);
        }

        // Empty portion - darker/dimmed blocks
        if (filledWidth < innerWidth) {
            int16_t emptyX = x + CHAR_WIDTH + (filledWidth * CHAR_WIDTH);
            int16_t emptyWidth = (innerWidth - filledWidth) * CHAR_WIDTH;
            // Use a dimmed version of the foreground color for empty portion
            uint16_t dimColor = ((fgColor >> 1) & 0x7BEF);  // Divide RGB by 2
            tft->fillRect(emptyX, y + 2, emptyWidth, CHAR_HEIGHT - 4, dimColor);
        }
    }

    // Draw a list item with selection highlight
    void drawListItem(uint8_t col, uint8_t row, uint8_t width,
                      const char* text, bool selected,
                      uint16_t fgNormal = DOS_WHITE, uint16_t bgNormal = DOS_BLUE,
                      uint16_t fgSelected = DOS_BLACK, uint16_t bgSelected = DOS_CYAN) {
        if (col >= GRID_COLS || row >= GRID_ROWS || !text) return;

        uint16_t fg = selected ? fgSelected : fgNormal;
        uint16_t bg = selected ? bgSelected : bgNormal;

        // Fill the entire row width
        fillGridRect(col, row, width, 1, bg);

        // Draw selection indicator and text
        if (selected) {
            drawText(col, row, " > ", fg, bg);
            drawText(col + 3, row, text, fg, bg);
        } else {
            drawText(col, row, "   ", fg, bg);
            drawText(col + 3, row, text, fg, bg);
        }
    }

    // Draw a horizontal line
    void drawHLine(uint8_t col, uint8_t row, uint8_t width,
                   uint16_t color = DOS_WHITE) {
        if (col >= GRID_COLS || row >= GRID_ROWS) return;

        if (col + width > GRID_COLS) {
            width = GRID_COLS - col;
        }

        int16_t x, y;
        gridToPixel(col, row, &x, &y);

        // Draw line in middle of character cell
        tft->drawFastHLine(x, y + CHAR_HEIGHT / 2, width * CHAR_WIDTH, color);
    }

    // Draw a vertical line
    void drawVLine(uint8_t col, uint8_t row, uint8_t height,
                   uint16_t color = DOS_WHITE) {
        if (col >= GRID_COLS || row >= GRID_ROWS) return;

        if (row + height > GRID_ROWS) {
            height = GRID_ROWS - row;
        }

        int16_t x, y;
        gridToPixel(col, row, &x, &y);

        // Draw line in middle of character cell
        tft->drawFastVLine(x + CHAR_WIDTH / 2, y, height * CHAR_HEIGHT, color);
    }

    // Draw a status bar at the bottom of the screen
    void drawStatusBar(const char* leftText, const char* rightText = nullptr,
                      uint16_t fgColor = DOS_BLACK,
                      uint16_t bgColor = DOS_LIGHT_GRAY) {
        // Status bar on bottom row
        fillGridRect(0, GRID_ROWS - 1, GRID_COLS, 1, bgColor);

        if (leftText) {
            drawText(1, GRID_ROWS - 1, leftText, fgColor, bgColor);
        }

        if (rightText) {
            uint8_t rightLen = strlen(rightText);
            if (rightLen < GRID_COLS - 1) {
                drawText(GRID_COLS - rightLen - 1, GRID_ROWS - 1, rightText, fgColor, bgColor);
            }
        }
    }

    // Get grid dimensions (for layout calculations)
    uint8_t getCols() const { return GRID_COLS; }
    uint8_t getRows() const { return GRID_ROWS; }


    // ============================================
    // STATUS NOTIFICATION SYSTEM
    // ============================================

    // Show a temporary status notification on bottom-right (uses Layer 2 overlay)
    // Duration in milliseconds (e.g., 3000 for 3 seconds)
    // Color should be a semantic color: DOS_GREEN (success), DOS_YELLOW (warning), DOS_RED (error)
    void showStatusNotification(const char* message, uint32_t durationMs = 3000,
                                uint16_t fgColor = DOS_BLACK, uint16_t bgColor = DOS_YELLOW) {
        statusNotificationMessage = message;
        statusNotificationExpireTime = millis() + durationMs;
        statusNotificationColor = bgColor;
        statusNotificationFgColor = fgColor;
        statusNotificationActive = true;

        Serial.print("[RetroUI] Showing notification: ");
        Serial.print(message);
        Serial.print(" (expires at: ");
        Serial.print(statusNotificationExpireTime);
        Serial.println(")");

        // Draw immediately on Layer 2
        drawStatusNotification();
    }

    // Update status notification (call in main loop to handle expiration)
    // Returns: 1 if active, 0 if inactive, -1 if just expired (clearing in progress)
    int updateStatusNotification() {
        if (!statusNotificationActive) {
            return 0;  // Not active
        }

        uint32_t now = millis();
        if (now >= statusNotificationExpireTime) {
            Serial.print("[RetroUI] Notification expired! now=");
            Serial.print(now);
            Serial.print(" expireTime=");
            Serial.println(statusNotificationExpireTime);

            statusNotificationActive = false;
            clearStatusNotification();  // Clear Layer 2 overlay
            return -1;  // Just expired - cleared
        }

        return 1;  // Still active
    }

    // Draw current status notification (if active) on bottom-right
    void drawStatusNotification() {
        if (!statusNotificationActive) {
            return;
        }

        uint8_t msgLen = statusNotificationMessage.length();
        if (msgLen == 0 || msgLen > GRID_COLS) {
            return;
        }

        // Draw notification on right side of bottom row with padding
        uint8_t col = GRID_COLS - msgLen - 2;  // 2 char padding from right edge
        drawText(col, GRID_ROWS - 1, statusNotificationMessage.c_str(),
                statusNotificationFgColor, statusNotificationColor);
    }

    // Clear status notification immediately
    void clearStatusNotification() {
        Serial.println("[RetroUI] Clearing notification");

        statusNotificationActive = false;
        statusNotificationMessage = "";

        // Erase the entire right half of footer to fully clear any notification
        // (Some notifications are longer than others, so clear generously)
        fillGridRect(GRID_COLS / 2, GRID_ROWS - 1, GRID_COLS / 2, 1, DOS_LIGHT_GRAY);
    }

    // Check if notification is currently active
    bool hasActiveNotification() const {
        return statusNotificationActive;
    }

    // ============================================
    // REGION SAVE/RESTORE (DOS-style)
    // ============================================

    /**
     * Saved region handle for save/restore operations
     * Used for modals, popups, and temporary overlays
     */
    struct SavedRegion {
        uint8_t col;
        uint8_t row;
        uint8_t width;
        uint8_t height;
        uint16_t* pixelBuffer;  // Pixel data (width*8 x height*16)
        uint32_t bufferSize;

        SavedRegion() : col(0), row(0), width(0), height(0), pixelBuffer(nullptr), bufferSize(0) {}

        ~SavedRegion() {
            if (pixelBuffer) {
                free(pixelBuffer);
                pixelBuffer = nullptr;
            }
        }

        bool isValid() const { return pixelBuffer != nullptr; }
    };

    /**
     * Save a screen region for later restoration (DOS text-mode style)
     * @param col - Starting column (grid)
     * @param row - Starting row (grid)
     * @param width - Width in characters
     * @param height - Height in characters
     * @return SavedRegion object (check isValid() before use)
     *
     * Example:
     *   SavedRegion* saved = ui->saveRegion(20, 10, 60, 15);
     *   // Draw modal...
     *   ui->restoreRegion(saved);
     *   delete saved;
     */
    SavedRegion* saveRegion(uint8_t col, uint8_t row, uint8_t width, uint8_t height) {
        // Validate parameters
        if (col >= GRID_COLS || row >= GRID_ROWS || width == 0 || height == 0) {
            Serial.println("[RetroUI] ERROR: Invalid save region parameters");
            return nullptr;
        }

        // Clamp to grid boundaries
        if (col + width > GRID_COLS) width = GRID_COLS - col;
        if (row + height > GRID_ROWS) height = GRID_ROWS - row;

        // Allocate saved region
        SavedRegion* region = new SavedRegion();
        region->col = col;
        region->row = row;
        region->width = width;
        region->height = height;

        // Calculate pixel dimensions
        uint16_t pixelWidth = width * CHAR_WIDTH;
        uint16_t pixelHeight = height * CHAR_HEIGHT;
        region->bufferSize = pixelWidth * pixelHeight;

        // Allocate pixel buffer
        region->pixelBuffer = (uint16_t*)malloc(region->bufferSize * sizeof(uint16_t));
        if (!region->pixelBuffer) {
            Serial.println("[RetroUI] ERROR: Failed to allocate save region buffer");
            delete region;
            return nullptr;
        }

        // Read pixels from screen
        int16_t x, y;
        gridToPixel(col, row, &x, &y);

        // RA8875 doesn't have efficient pixel read, so we'll use a workaround:
        // For now, just fill with background color as placeholder
        // TODO: Implement actual pixel reading if RA8875 library supports it
        for (uint32_t i = 0; i < region->bufferSize; i++) {
            region->pixelBuffer[i] = currentBgColor;
        }

        Serial.print("[RetroUI] Saved region: ");
        Serial.print(width);
        Serial.print("x");
        Serial.print(height);
        Serial.print(" (");
        Serial.print(region->bufferSize * 2);
        Serial.println(" bytes)");

        return region;
    }

    /**
     * Restore a previously saved screen region
     * @param region - SavedRegion returned from saveRegion()
     */
    void restoreRegion(SavedRegion* region) {
        if (!region || !region->isValid()) {
            Serial.println("[RetroUI] ERROR: Invalid region to restore");
            return;
        }

        // Restore pixels to screen
        // RA8875 pixel-by-pixel writing is slow, so we use fillRect for now
        // TODO: Implement proper pixel blitting if needed
        fillGridRect(region->col, region->row, region->width, region->height, region->pixelBuffer[0]);

        Serial.print("[RetroUI] Restored region: ");
        Serial.print(region->width);
        Serial.print("x");
        Serial.println(region->height);
    }
};

#endif // RETRO_UI_H