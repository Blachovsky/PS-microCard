#ifndef OLED_H
#define OLED_H

/* SSD1306 OLED driver and application status screens. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OLED_RESULT_OK = 0,
    OLED_ERROR_SPI_INIT_FAILED,
    OLED_ERROR_SPI_WRITE_FAILED,
} oled_result_t;

/*
 * Minimal SSD1306 driver for the DFRobot DFR0650 in 4-wire SPI mode.
 * All functions should be called only from core 1.
 */
oled_result_t oled_init(void);
void oled_set_display_enabled(bool enabled);
uint32_t oled_get_update_count(void);
void oled_show_text(const char *line0,
                    const char *line1,
                    const char *line2,
                    const char *line3);
void oled_show_ready_for_image(const char *image_name);
void oled_show_saving(uint16_t frame_addr);
void oled_show_sd_error(void);

#endif // OLED_H
