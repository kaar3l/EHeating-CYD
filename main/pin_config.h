#pragma once

// DS18B20 one-wire bus
#define ONE_WIRE_BUS        1

// Relays (active LOW - inverted)
#define RELAY1_PIN          40
#define RELAY2_PIN          2

// Backlight (LEDC)
#define LCD_BACKLIGHT_PIN   38

// SPI for ST7701S init commands
#define LCD_SPI_CLK         48
#define LCD_SPI_MOSI        47
#define LCD_SPI_CS          39

// RGB panel signals
#define LCD_DE_PIN          18
#define LCD_HSYNC_PIN       16
#define LCD_VSYNC_PIN       17
#define LCD_PCLK_PIN        21

// RGB data pins: R[4:0], G[5:0], B[4:0]
#define LCD_R0              11
#define LCD_R1              12
#define LCD_R2              13
#define LCD_R3              14
#define LCD_R4              0

#define LCD_G0              8
#define LCD_G1              20
#define LCD_G2              3
#define LCD_G3              46
#define LCD_G4              9
#define LCD_G5              10

#define LCD_B0              4
#define LCD_B1              5
#define LCD_B2              6
#define LCD_B3              7
#define LCD_B4              15

#define LCD_WIDTH           480
#define LCD_HEIGHT          480
#define LCD_PCLK_HZ         (12 * 1000 * 1000)
