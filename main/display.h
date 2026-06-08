#pragma once
#include <stdint.h>

typedef enum { SCREEN_STATUS, SCREEN_SETTINGS } screen_mode_t;

/* R and B pre-swapped to match panel's physical BGR subpixel order */
#define COLOR_BLACK   0x0000u
#define COLOR_WHITE   0xFFFFu
#define COLOR_RED     0x001Fu  /* R↔B: was 0xF800 */
#define COLOR_GREEN   0x07E0u
#define COLOR_BLUE    0xF800u  /* R↔B: was 0x001F */
#define COLOR_YELLOW  0x07FFu  /* R↔B: was 0xFFE0 */
#define COLOR_CYAN    0xFFE0u  /* R↔B: was 0x07FF */
#define COLOR_ORANGE  0x053Fu  /* R↔B: was 0xFD20 */
#define COLOR_GRAY    0x8410u

#define FONT_SCALE    2   // 8x8 font scaled 2x = 16x16px per char

void display_init(void);
void display_status_msg(const char *line1, const char *line2, uint16_t color1);
void display_boot_screen(void);
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void display_draw_logo(int x, int y, uint16_t fg, uint16_t bg, int scale);
void display_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
void display_update_status(void);

void display_set_brightness(int pct);          // 0-100 %
void display_set_screen(screen_mode_t mode);   // switch + clear
screen_mode_t display_get_screen(void);
void display_show_settings(void);              // draw/redraw settings page
