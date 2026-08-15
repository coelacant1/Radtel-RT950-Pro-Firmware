/*
 * zone_browser.c - Zone selection browser for the RT-950 Pro
 *
 * Reads zone names from SPI flash at 0x00C000, 16-byte stride, 12 bytes used.
 * Address confirmed by dumping a physical radio: the ten OEM defaults
 * "ZoneOne".."ZoneTen" are stored there.
 * User selects with encoder + MENU, cancels with EXIT.
 */

#include "app/zone_browser.h"
#include "app/keypad.h"
#include "app/display.h"
#include "app/font.h"
#include "drivers/lcd.h"
#include "drivers/spi.h"
#include "drivers/flash_layout.h"

/* State ---------------------------------------------------------------- */

static uint8_t active;
static uint8_t cursor;       /* 0..FLASH_ZONE_MAX-1 */
static uint8_t selected;     /* confirmed selection, or 0xFF */

void zone_browser_open(void)
{
    active = 1;
    cursor = 0;
    selected = 0xFF;
}

void zone_browser_close(void)
{
    active = 0;
}

uint8_t zone_browser_is_active(void) { return active; }
uint8_t zone_browser_get_selected(void) { return selected; }

/* Flash read ----------------------------------------------------------- */

void zone_read_name(uint8_t index, char *buf)
{
    if (index >= FLASH_ZONE_MAX) {
        buf[0] = '\0';
        return;
    }

    uint8_t raw[FLASH_ZONE_NAME_SIZE];
    /* stride (16) and read length (12) differ — see flash_layout.h */
    uint32_t addr = FLASH_ADDR_ZONE_NAMES + (uint32_t)index * FLASH_ZONE_NAME_STRIDE;
    spi_flash_read(addr, raw, FLASH_ZONE_NAME_SIZE);

    /* Copy, converting 0xFF padding to NUL */
    uint8_t i;
    for (i = 0; i < FLASH_ZONE_NAME_SIZE; i++) {
        if (raw[i] == 0xFF || raw[i] == '\0') break;
        buf[i] = (char)raw[i];
    }
    buf[i] = '\0';
}

/* Input ---------------------------------------------------------------- */

void zone_browser_handle_key(uint8_t key)
{
    if (!active) return;

    if (key == KEY_C_MENU) {
        selected = cursor;
        active = 0;
        return;
    }

    if (key == KEY_D_BAND || key == KEY_HASH) {
        selected = 0xFF;
        active = 0;
        return;
    }
}

void zone_browser_handle_encoder(int8_t direction)
{
    if (!active) return;

    if (direction > 0 && cursor < FLASH_ZONE_MAX - 1)
        cursor++;
    else if (direction < 0 && cursor > 0)
        cursor--;
}

/* Drawing -------------------------------------------------------------- */

void zone_browser_draw(void)
{
    if (!active) return;

    lcd_fill_rect(0, 0, LCD_WIDTH, 200, COLOR_BLACK);

    /* Header */
    font_draw_string(FONT_SMALL, 4, 4, "Zone Select", COLOR_YELLOW, COLOR_BLACK);
    lcd_fill_rect(0, 18, LCD_WIDTH, 1, COLOR_DARK_GRAY);

    /* List all zones */
    char name[FLASH_ZONE_NAME_SIZE + 1];
    uint16_t y = 24;

    for (uint8_t i = 0; i < FLASH_ZONE_MAX; i++) {
        uint8_t is_sel = (i == cursor);
        uint16_t fg = is_sel ? COLOR_WHITE : COLOR_GRAY;
        uint16_t bg = is_sel ? COLOR_DARK_GRAY : COLOR_BLACK;

        if (is_sel)
            lcd_fill_rect(0, y, LCD_WIDTH, 16, bg);

        /* Zone number */
        char num[4];
        num[0] = (char)('0' + ((i + 1) / 10));
        num[1] = (char)('0' + ((i + 1) % 10));
        num[2] = '.';
        num[3] = '\0';
        if (num[0] == '0') num[0] = ' ';
        font_draw_string(FONT_SMALL, 4, y + 1, num, fg, bg);

        /* Zone name */
        zone_read_name(i, name);
        if (name[0] == '\0') {
            font_draw_string(FONT_SMALL, 30, y + 1, "(empty)",
                             COLOR_DARK_GRAY, bg);
        } else {
            font_draw_string(FONT_SMALL, 30, y + 1, name, fg, bg);
        }

        y += 16;
    }

    /* Footer */
    lcd_fill_rect(0, y + 4, LCD_WIDTH, 1, COLOR_DARK_GRAY);
    font_draw_string(FONT_SMALL, 4, y + 8, "MENU=Select  EXIT=Back",
                     COLOR_GRAY, COLOR_BLACK);
}
