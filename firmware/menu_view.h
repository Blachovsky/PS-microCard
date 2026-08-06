#ifndef MENU_VIEW_H
#define MENU_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void menu_view_show_message(const char *line0,
                            const char *line1,
                            const char *line2,
                            const char *line3);
void menu_view_show_status(const char *image_name);
void menu_view_show_no_card(void);
void menu_view_show_card_error(const char *error_text);
void menu_view_show_main(const char *item);
void menu_view_show_image_browser(const char *title,
                                  const char *image_name,
                                  size_t image_index,
                                  size_t image_count,
                                  bool is_active);
void menu_view_show_saves(const char *active_image_name,
                          const char *save_file_name,
                          uint8_t slot,
                          uint8_t blocks,
                          size_t save_index,
                          size_t save_count);
void menu_view_show_delete_confirm(const char *image_name, bool confirmed);
void menu_view_show_creating_image(void);
void menu_view_show_loading_image(const char *image_name);
void menu_view_show_deleting_image(const char *image_name);

#endif // MENU_VIEW_H
