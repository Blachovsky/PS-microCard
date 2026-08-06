#ifndef MENU_DISPLAY_H
#define MENU_DISPLAY_H

#include "oled.h"

#include <stdbool.h>

oled_result_t menu_display_init(void);
bool menu_display_is_available(void);
bool menu_display_is_awake(void);
bool menu_display_wake(void);
void menu_display_note_activity(void);
bool menu_display_poll_updates(void);
bool menu_display_poll_idle(void);

#endif // MENU_DISPLAY_H
