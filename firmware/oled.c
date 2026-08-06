#include "oled.h"

#include "app_log.h"
#include "hardware_config.h"
#include "oled_font.h"

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OLED_PAGE_COUNT       (OLED_HEIGHT / 8u)
#define OLED_FRAMEBUFFER_SIZE (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_TEXT_LINE_COUNT  4u
#define LOG_TAG               "oled"

static uint8_t framebuffer[OLED_FRAMEBUFFER_SIZE];
static uint8_t last_sent_framebuffer[OLED_FRAMEBUFFER_SIZE];
static bool oled_ready;
static bool oled_display_enabled;
static uint32_t oled_update_count;

static inline void oled_select(void) {
    gpio_put(OLED_CS_PIN, 0);
}

static inline void oled_deselect(void) {
    gpio_put(OLED_CS_PIN, 1);
}

static oled_result_t oled_write_command_bytes(const uint8_t *commands,
                                              size_t count) {
    if (commands == NULL || count == 0u) {
        return OLED_RESULT_OK;
    }

    gpio_put(OLED_DC_PIN, 0);
    oled_select();
    int written = spi_write_blocking(OLED_SPI_PORT, commands, count);
    oled_deselect();
    return written >= 0 && (size_t)written == count
            ? OLED_RESULT_OK
            : OLED_ERROR_SPI_WRITE_FAILED;
}

static oled_result_t oled_write_data(const uint8_t *data, size_t count) {
    if (data == NULL || count == 0u) {
        return OLED_RESULT_OK;
    }

    gpio_put(OLED_DC_PIN, 1);
    oled_select();
    int written = spi_write_blocking(OLED_SPI_PORT, data, count);
    oled_deselect();
    return written >= 0 && (size_t)written == count
            ? OLED_RESULT_OK
            : OLED_ERROR_SPI_WRITE_FAILED;
}

static oled_result_t oled_flush(void) {
    for (uint8_t page = 0u; page < OLED_PAGE_COUNT; ++page) {
        const uint8_t set_page[] = {
            (uint8_t)(0xB0u | page), // page start address
            0x00u,                  // lower column start address
            0x10u,                  // higher column start address
        };

        oled_result_t result = oled_write_command_bytes(set_page,
                                                        sizeof(set_page));
        if (result != OLED_RESULT_OK) {
            return result;
        }

        result = oled_write_data(
                &framebuffer[(size_t)page * OLED_WIDTH],
                OLED_WIDTH);
        if (result != OLED_RESULT_OK) {
            return result;
        }
    }

    return OLED_RESULT_OK;
}

static void oled_draw_text_line(uint8_t text_line, const char *text) {
    if (text_line >= OLED_TEXT_LINE_COUNT || text == NULL) {
        return;
    }

    /* Each text row uses one 8 px page, with one blank page between rows. */
    uint8_t page = (uint8_t)(text_line * 2u);
    size_t page_offset = (size_t)page * OLED_WIDTH;
    size_t x = 1u;

    memset(&framebuffer[page_offset], 0, OLED_WIDTH);

    while (*text != '\0' && x + OLED_FONT_GLYPH_WIDTH < OLED_WIDTH) {
        const uint8_t *glyph = oled_font_get_glyph(*text++);

        for (size_t column = 0; column < OLED_FONT_GLYPH_WIDTH; ++column) {
            framebuffer[page_offset + x++] = glyph[column];
        }

        framebuffer[page_offset + x++] = 0;
    }
}

static void oled_show_lines(const char *line0,
                            const char *line1,
                            const char *line2,
                            const char *line3) {
    if (!oled_ready) {
        return;
    }

    memset(framebuffer, 0, sizeof(framebuffer));
    oled_draw_text_line(0, line0);
    oled_draw_text_line(1, line1);
    oled_draw_text_line(2, line2);
    oled_draw_text_line(3, line3);

    bool changed = memcmp(framebuffer,
                          last_sent_framebuffer,
                          sizeof(framebuffer)) != 0;
    if (!changed) {
        return;
    }

    oled_result_t result = oled_flush();
    if (result != OLED_RESULT_OK) {
        LOG_ERROR(LOG_TAG, "Framebuffer transfer failed: result=%d", result);
        return;
    }

    memcpy(last_sent_framebuffer, framebuffer, sizeof(last_sent_framebuffer));
    ++oled_update_count;
}

oled_result_t oled_init(void) {
    oled_ready = false;
    oled_display_enabled = false;

    /* Keep CS inactive before enabling SPI pins to avoid stray commands. */
    gpio_init(OLED_CS_PIN);
    gpio_put(OLED_CS_PIN, 1);
    gpio_set_dir(OLED_CS_PIN, GPIO_OUT);

    gpio_init(OLED_DC_PIN);
    gpio_put(OLED_DC_PIN, 0);
    gpio_set_dir(OLED_DC_PIN, GPIO_OUT);

    gpio_set_function(OLED_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(OLED_MOSI_PIN, GPIO_FUNC_SPI);

    if (spi_init(OLED_SPI_PORT, OLED_BAUD_RATE) == 0u) {
        return OLED_ERROR_SPI_INIT_FAILED;
    }
    spi_set_format(OLED_SPI_PORT,
                   8,
                   SPI_CPOL_0,
                   SPI_CPHA_0,
                   SPI_MSB_FIRST);

    /* The module has no exposed RESET pin, so give it time after power-up. */
    busy_wait_ms(100);

#if OLED_ROTATE_180
    const uint8_t segment_remap = 0xA0;
    const uint8_t com_scan = 0xC0;
#else
    const uint8_t segment_remap = 0xA1;
    const uint8_t com_scan = 0xC8;
#endif

    const uint8_t init_commands[] = {
        0xAE,             // display off
        0xD5, 0x80,       // display clock divide
        0xA8, 0x3F,       // multiplex 1/64
        0xD3, 0x00,       // display offset
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x02,       // page addressing mode
        segment_remap,
        com_scan,
        0xDA, 0x12,       // COM pins configuration
        0x81, 0x7F,       // contrast
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOM detect
        0xA4,             // display follows RAM
        0xA6,             // normal display
        0x2E,             // stop scrolling
    };

    oled_result_t result = oled_write_command_bytes(init_commands,
                                                    sizeof(init_commands));
    if (result != OLED_RESULT_OK) {
        return result;
    }

    memset(framebuffer, 0, sizeof(framebuffer));
    result = oled_flush();
    if (result != OLED_RESULT_OK) {
        return result;
    }

    const uint8_t display_on = 0xAF;
    result = oled_write_command_bytes(&display_on, 1u);
    if (result != OLED_RESULT_OK) {
        return result;
    }

    memcpy(last_sent_framebuffer, framebuffer, sizeof(last_sent_framebuffer));
    oled_update_count = 0u;

    oled_ready = true;
    oled_display_enabled = true;
    return OLED_RESULT_OK;
}

void oled_set_display_enabled(bool enabled) {
    if (!oled_ready || oled_display_enabled == enabled) {
        return;
    }

    oled_result_t result = OLED_RESULT_OK;

    if (enabled) {
        /* Restore addressing and RAM before making the panel visible. */
        result = oled_flush();
    }

    if (result == OLED_RESULT_OK) {
        const uint8_t command = enabled ? 0xAF : 0xAE;
        result = oled_write_command_bytes(&command, 1u);
    }

    if (result != OLED_RESULT_OK) {
        LOG_ERROR(LOG_TAG,
                  "Display %s failed: result=%d",
                  enabled ? "enable" : "disable",
                  result);
        return;
    }

    oled_display_enabled = enabled;
}

uint32_t oled_get_update_count(void) {
    return oled_update_count;
}

void oled_show_text(const char *line0,
                    const char *line1,
                    const char *line2,
                    const char *line3) {
    oled_show_lines(line0, line1, line2, line3);
}

void oled_show_ready_for_image(const char *image_name) {
    oled_show_lines("PS1 MEMORY CARD",
                    "ACTIVE IMAGE",
                    image_name,
                    "L.B. SAVES R.B. MENU");
}

void oled_show_saving(uint16_t frame_addr) {
    char frame_line[22];
    (void)snprintf(frame_line,
                   sizeof(frame_line),
                   "FRAME: %04u",
                   (unsigned)frame_addr);

    oled_show_lines("PS1 MEMORY CARD",
                    "SD: SAVING...",
                    frame_line,
                    "DO NOT POWER OFF");
}

void oled_show_sd_error(void) {
    oled_show_lines("PS1 MEMORY CARD",
                    "SD ERROR",
                    "RETRYING...",
                    "CHECK MICROSD");
}
