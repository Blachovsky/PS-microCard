#ifndef OLED_FONT_H
#define OLED_FONT_H

/* Compact glyph lookup used by the OLED driver. */

#include <stdint.h>

#define OLED_FONT_GLYPH_WIDTH 5u

const uint8_t *oled_font_get_glyph(char character);

#endif // OLED_FONT_H
