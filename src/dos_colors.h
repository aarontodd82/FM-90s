#ifndef DOS_COLORS_H
#define DOS_COLORS_H

// Classic IBM CGA/EGA 16-color palette (RGB565 format)
// These colors match the original DOS text mode colors

// Dark colors (0-7)
#define DOS_BLACK       0x0000  // 0: Black
#define DOS_BLUE        0x0014  // 1: Blue
#define DOS_GREEN       0x0540  // 2: Green
#define DOS_CYAN        0x0555  // 3: Cyan
#define DOS_RED         0xA800  // 4: Red
#define DOS_MAGENTA     0xA815  // 5: Magenta
#define DOS_BROWN       0xA2A0  // 6: Brown (dark yellow)
#define DOS_LIGHT_GRAY  0xAD55  // 7: Light Gray

// Bright colors (8-15)
#define DOS_DARK_GRAY   0x52AA  // 8: Dark Gray
#define DOS_BRIGHT_BLUE 0x52BF  // 9: Bright Blue
#define DOS_BRIGHT_GREEN 0x57EA // 10: Bright Green
#define DOS_BRIGHT_CYAN 0x57FF  // 11: Bright Cyan
#define DOS_BRIGHT_RED  0xFAAA  // 12: Bright Red
#define DOS_PINK        0xFABF  // 13: Pink (Bright Magenta)
#define DOS_YELLOW      0xFFEA  // 14: Yellow (Bright Brown)
#define DOS_WHITE       0xFFFF  // 15: White

// Common color combinations for DOS-style UI
#define DOS_DEFAULT_BG  DOS_BLUE        // Classic blue background
#define DOS_DEFAULT_FG  DOS_WHITE       // White text
#define DOS_MENU_BG     DOS_BLUE        // Menu background
#define DOS_MENU_FG     DOS_WHITE       // Menu text
#define DOS_SELECT_BG   DOS_CYAN        // Selected item background
#define DOS_SELECT_FG   DOS_BLACK       // Selected item text
#define DOS_STATUS_BG   DOS_LIGHT_GRAY  // Status bar background
#define DOS_STATUS_FG   DOS_BLACK       // Status bar text
#define DOS_ERROR_BG    DOS_RED         // Error background
#define DOS_ERROR_FG    DOS_WHITE       // Error text

#endif // DOS_COLORS_H