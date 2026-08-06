#include "menu_view.h"

#include "oled.h"

#include <stdio.h>

void menu_view_show_message(const char *line0,
                            const char *line1,
                            const char *line2,
                            const char *line3) {
    oled_show_text(line0, line1, line2, line3);
}

void menu_view_show_status(const char *image_name) {
    oled_show_ready_for_image(image_name);
}

void menu_view_show_no_card(void) {
    oled_show_text("MICROSD CARD",
                   "NOT INSERTED",
                   "INSERT CARD",
                   "");
}

void menu_view_show_card_error(const char *error_text) {
    oled_show_text("MICROSD ERROR",
                   error_text,
                   "REINSERT CARD",
                   "");
}

void menu_view_show_main(const char *item) {
    char item_line[22];

    (void)snprintf(item_line,
                   sizeof(item_line),
                   "> %s",
                   item);

    oled_show_text("MENU",
                   item_line,
                   "L.B. NEXT R.B. OK",
                   "HOLD R.B. BACK");
}

void menu_view_show_image_browser(const char *title,
                                  const char *image_name,
                                  size_t image_index,
                                  size_t image_count,
                                  bool is_active) {
    char index_line[22];

    if (image_count == 0u) {
        oled_show_text(title,
                       "NO IMAGES",
                       "",
                       "HOLD R.B. BACK");
        return;
    }

    (void)snprintf(index_line,
                   sizeof(index_line),
                   "%02hhu/%02hhu %.12s",
                   (unsigned)(uint8_t)(image_index + 1u),
                   (unsigned)(uint8_t)image_count,
                   image_name);

    oled_show_text(title,
                   index_line,
                   is_active ? "ACTIVE" : "",
                   "L.B. NEXT R.B. OK");
}

void menu_view_show_saves(const char *active_image_name,
                          const char *save_file_name,
                          uint8_t slot,
                          uint8_t blocks,
                          size_t save_index,
                          size_t save_count) {
    char count_line[22];
    char detail_line[22];

    if (save_count == 0u) {
        oled_show_text("SAVES",
                       "NO SAVES",
                       active_image_name,
                       "HOLD R.B. BACK");
        return;
    }

    (void)snprintf(count_line,
                   sizeof(count_line),
                   "SAVE %02u/%02u",
                   (unsigned)(save_index + 1u),
                   (unsigned)save_count);
    (void)snprintf(detail_line,
                   sizeof(detail_line),
                   "SLOT %02u BLOCKS %02u",
                   (unsigned)slot,
                   (unsigned)blocks);

    oled_show_text(count_line,
                   save_file_name,
                   detail_line,
                   "HOLD R.B. BACK");
}

void menu_view_show_delete_confirm(const char *image_name, bool confirmed) {
    oled_show_text("DELETE IMAGE",
                   image_name,
                   confirmed ? "CONFIRM: YES" : "CONFIRM: NO",
                   "L.B. TOGGLE R.B. OK");
}

void menu_view_show_creating_image(void) {
    oled_show_text("CREATING IMAGE",
                   "PLEASE WAIT",
                   "",
                   "");
}

void menu_view_show_loading_image(const char *image_name) {
    oled_show_text("LOADING IMAGE",
                   image_name,
                   "PLEASE WAIT",
                   "");
}

void menu_view_show_deleting_image(const char *image_name) {
    oled_show_text("DELETING IMAGE",
                   image_name,
                   "PLEASE WAIT",
                   "");
}
