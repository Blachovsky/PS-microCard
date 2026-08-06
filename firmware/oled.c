#include "oled.h"

#include "hardware_config.h"

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OLED_PAGE_COUNT       (OLED_HEIGHT / 8u)
#define OLED_FRAMEBUFFER_SIZE (OLED_WIDTH * OLED_PAGE_COUNT)
#define OLED_TEXT_LINE_COUNT  4u

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
    static const uint8_t set_window[] = {
        0x21, 0x00, OLED_WIDTH - 1u,
        0x22, 0x00, OLED_PAGE_COUNT - 1u,
    };

    oled_result_t result = oled_write_command_bytes(set_window,
                                                    sizeof(set_window));
    if (result != OLED_RESULT_OK) {
        return result;
    }

    return oled_write_data(framebuffer, sizeof(framebuffer));
}

static const uint8_t *glyph_for(char c) {
    /* 5x7 column font covering the characters used by the status screens. */
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};

    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
        {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
        {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
        {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
        {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
        {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    };

    static const uint8_t letters[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
        {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
        {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
        {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
        {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
        {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
        {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
        {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
        {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
        {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
        {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
        {0x46, 0x49, 0x49, 0x49, 0x31}, // S
        {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
        {0x63, 0x14, 0x08, 0x14, 0x63}, // X
        {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
        {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    };

    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t greater[5] = {0x41, 0x22, 0x14, 0x08, 0x00};
    static const uint8_t underscore[5] = {0x40, 0x40, 0x40, 0x40, 0x40};

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }

    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }

    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }

    switch (c) {
        case ' ': return blank;
        case ':': return colon;
        case '.': return dot;
        case '-': return dash;
        case '/': return slash;
        case '>': return greater;
        case '_': return underscore;
        default:  return question;
    }
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

    while (*text != '\0' && x + 5u < OLED_WIDTH) {
        const uint8_t *glyph = glyph_for(*text++);

        for (size_t column = 0; column < 5u; ++column) {
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
    (void)oled_flush();

    if (changed) {
        memcpy(last_sent_framebuffer, framebuffer, sizeof(last_sent_framebuffer));
        ++oled_update_count;
    }
}

oled_result_t oled_init(void) {
    oled_ready = false;
    oled_display_enabled = false;

    if (spi_init(OLED_SPI_PORT, OLED_BAUD_RATE) == 0u) {
        return OLED_ERROR_SPI_INIT_FAILED;
    }
    spi_set_format(OLED_SPI_PORT,
                   8,
                   SPI_CPOL_0,
                   SPI_CPHA_0,
                   SPI_MSB_FIRST);

    gpio_set_function(OLED_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(OLED_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(OLED_CS_PIN);
    gpio_set_dir(OLED_CS_PIN, GPIO_OUT);
    gpio_put(OLED_CS_PIN, 1);

    gpio_init(OLED_DC_PIN);
    gpio_set_dir(OLED_DC_PIN, GPIO_OUT);
    gpio_put(OLED_DC_PIN, 0);

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
        0x20, 0x00,       // horizontal addressing mode
        segment_remap,
        com_scan,
        0xDA, 0x12,       // COM pins configuration
        0x81, 0x7F,       // contrast
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOM detect
        0xA4,             // display follows RAM
        0xA6,             // normal display
        0x2E,             // stop scrolling
        0xAF,             // display on
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

    const uint8_t command = enabled ? 0xAF : 0xAE;
    if (oled_write_command_bytes(&command, 1u) == OLED_RESULT_OK) {
        oled_display_enabled = enabled;
    }
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
