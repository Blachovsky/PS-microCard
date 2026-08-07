#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include "menu/menu_input.h"
#include "micro_sd/micro_sd.h"

#include <stdbool.h>

void menu_controller_init(void);
void menu_controller_show_status(void);
void menu_controller_show_no_card(void);
void menu_controller_show_card_error(micro_sd_result_t result);
void menu_controller_render_current(void);
void menu_controller_handle_event(menu_input_event_t event);
void menu_controller_poll(bool render_enabled);

#endif // MENU_CONTROLLER_H
