#ifndef MENU_DISPLAY_H
#define MENU_DISPLAY_H

#include "oled.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Display lifecycle and power management. */
oled_result_t menu_display_init(void);
bool menu_display_is_available(void);
bool menu_display_is_awake(void);
bool menu_display_wake(void);
void menu_display_note_activity(void);
bool menu_display_poll_updates(void);
bool menu_display_poll_idle(void);

/* Menu screen rendering. */
void menu_display_show_message(const char *line0,
                               const char *line1,
                               const char *line2,
                               const char *line3);
void menu_display_show_status(const char *image_name);
void menu_display_show_no_card(void);
void menu_display_show_card_error(const char *error_text);
void menu_display_show_main(const char *item);
void menu_display_show_image_browser(const char *title,
                                     const char *image_name,
                                     size_t image_index,
                                     size_t image_count,
                                     bool is_active);
void menu_display_show_saves(const char *active_image_name,
                             const char *save_file_name,
                             uint8_t slot,
                             uint8_t blocks,
                             size_t save_index,
                             size_t save_count);
void menu_display_show_delete_confirm(const char *image_name, bool confirmed);
void menu_display_show_creating_image(void);
void menu_display_show_loading_image(const char *image_name);
void menu_display_show_deleting_image(const char *image_name);

#endif // MENU_DISPLAY_H
