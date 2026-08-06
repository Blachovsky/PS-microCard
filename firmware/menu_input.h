#ifndef MENU_INPUT_H
#define MENU_INPUT_H

#include <stdbool.h>

typedef enum {
    MENU_INPUT_EVENT_NONE,
    MENU_INPUT_EVENT_NEXT_SHORT,
    MENU_INPUT_EVENT_NEXT_LONG,
    MENU_INPUT_EVENT_SELECT_SHORT,
    MENU_INPUT_EVENT_SELECT_LONG,
} menu_input_event_t;

void menu_input_init(void);
bool menu_input_any_pressed(void);
void menu_input_discard_current_press(void);
menu_input_event_t menu_input_poll(void);

#endif // MENU_INPUT_H
