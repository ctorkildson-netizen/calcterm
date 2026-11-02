/* stub/drivers/lcd.h -- the LCD as a frame memory, for screen.c on Linux */
#pragma once
#include "pico/stdlib.h"
#include "font.h"
#define WIDTH 320
#define HEIGHT 320
#define FRAME_HEIGHT 480
#define RGB(r,g,b) ((uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3)))
extern uint16_t lcd_mem[FRAME_HEIGHT][WIDTH];
extern int lcd_offset;
void lcd_set_background(uint16_t colour);
void lcd_clear_screen(void);
void lcd_enable_cursor(bool on);
void lcd_define_scrolling(uint16_t top, uint16_t bottom);
void lcd_scroll_up(void);
void lcd_blit(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void lcd_solid_rectangle(uint16_t colour, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
