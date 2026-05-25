#pragma once
#include <stdint.h>

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_ORANGE  0xFC00
#define COLOR_GRAY    0x8410

#define FONT_SCALE    3   // 8x8 font scaled 3x = 24x24px per char

void display_init(void);
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void display_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
void display_update_status(void);
