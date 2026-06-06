#pragma once

// DS18B20 one-wire bus
#define ONE_WIRE_BUS        27

// Relays (active LOW)
#define RELAY1_PIN          16
#define RELAY2_PIN          4

// ILI9341 SPI display (SPI2 / HSPI)
#define LCD_MOSI_PIN        13
#define LCD_SCLK_PIN        14
#define LCD_CS_PIN          15
#define LCD_DC_PIN          2
#define LCD_RST_PIN         12
#define LCD_BACKLIGHT_PIN   21

// MADCTL: MV=1 (landscape) + BGR=1 (panel wiring)
// Change to 0xA8 or 0x68 if image is mirrored/flipped
#define LCD_MADCTL          0x40

#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_FREQ_HZ     (40 * 1000 * 1000)
