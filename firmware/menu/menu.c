#include "menu/menu.h"

#include "logger/app_log.h"
#include "menu/menu_controller.h"
#include "menu/menu_display.h"
#include "menu/menu_input.h"
#include "menu/menu_storage.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"
#include "micro_sd/micro_sd.h"

#include "pico/stdlib.h"

#include <stdbool.h>

#define MENU_POLL_MS 10u
#define LOG_TAG      "menu"

/* Card lifecycle shared by startup, insertion and retry handling. */

static micro_sd_result_t reload_inserted_card(const char *initial_image_path) {
    micro_sd_result_t result;

    ps1_bus_set_card_present(false);

    if (!menu_storage_is_physically_present()) {
        micro_sd_handle_card_unavailable();
        menu_controller_show_no_card();
        return MICRO_SD_ERROR_CARD_NOT_PRESENT;
    }

    result = micro_sd_load_or_create_initial_image(initial_image_path);
    if (result != MICRO_SD_RESULT_OK) {
        micro_sd_handle_card_unavailable();
        menu_controller_show_card_error(result);
        return result;
    }

    ps1emu_storage_state_init();
    micro_sd_save_worker_init(micro_sd_active_image_path());
    ps1_bus_begin_card_swap_absent();
    ps1_bus_set_card_present(true);
    menu_storage_mark_ready();
    menu_controller_show_status();
    return MICRO_SD_RESULT_OK;
}

static void handle_card_removed(void) {
    (void)menu_display_wake();
    micro_sd_handle_card_unavailable();
    menu_controller_show_no_card();
}

/* Main menu task and subsystem coordination. */

void menu_task_run(const char *initial_image_path) {
    oled_result_t oled_result;
    bool ignore_buttons_until_release = false;
    micro_sd_result_t result;

    menu_controller_init();
    menu_input_init();
    menu_storage_init();
    micro_sd_save_worker_init(initial_image_path);
    ps1_bus_set_card_present(false);

    oled_result = menu_display_init();

    if (oled_result != OLED_RESULT_OK) {
        LOG_ERROR(LOG_TAG,
                  "OLED initialization failed: result=%d",
                  (int)oled_result);
    }

    if (menu_storage_is_present()) {
        result = reload_inserted_card(initial_image_path);

        if (result != MICRO_SD_RESULT_OK) {
            menu_storage_mark_error();
        }
    } else {
        micro_sd_handle_card_unavailable();
        menu_controller_show_no_card();
    }

    while (true) {
        menu_storage_event_t storage_event = menu_storage_poll();

        if (storage_event == MENU_STORAGE_EVENT_REMOVED) {
            handle_card_removed();
        } else if (storage_event == MENU_STORAGE_EVENT_INSERTED ||
                   storage_event == MENU_STORAGE_EVENT_RETRY_DUE) {
            bool card_inserted = storage_event == MENU_STORAGE_EVENT_INSERTED;

            if (card_inserted) {
                (void)menu_display_wake();
            }

            result = reload_inserted_card(initial_image_path);

            if (result == MICRO_SD_RESULT_OK) {
                if (!card_inserted && !menu_display_is_awake()) {
                    (void)menu_display_wake();
                }
            } else {
                menu_storage_mark_error();
            }
        } else if (storage_event == MENU_STORAGE_EVENT_PROBE_DUE) {
            result = micro_sd_check_active_image_accessible();

            if (result != MICRO_SD_RESULT_OK) {
                if (menu_storage_is_physically_present()) {
                    menu_storage_mark_error();
                    micro_sd_handle_card_unavailable();
                    menu_controller_show_card_error(result);
                } else {
                    menu_storage_mark_removed();
                    handle_card_removed();
                }
            }
        }

        if (menu_storage_is_ready()) {
            result = micro_sd_save_worker_poll();

            if (result != MICRO_SD_RESULT_OK) {
                if (menu_storage_is_physically_present()) {
                    menu_storage_mark_error();
                    menu_controller_show_card_error(result);
                } else {
                    menu_storage_mark_removed();
                    handle_card_removed();
                }
            }
        }

        menu_input_event_t event = menu_input_poll();
        bool button_pressed = menu_input_any_pressed();
        bool button_activity = button_pressed || event != MENU_INPUT_EVENT_NONE;
        bool handle_buttons = menu_storage_is_ready();

        if (menu_display_is_available()) {
            if (button_activity && menu_display_wake()) {
                menu_input_discard_current_press();
                event = MENU_INPUT_EVENT_NONE;
                button_pressed = menu_input_any_pressed();
                ignore_buttons_until_release = true;
                menu_controller_render_current();
            }

            if (!menu_display_is_awake()) {
                handle_buttons = false;
            } else if (ignore_buttons_until_release) {
                handle_buttons = false;

                if (!button_pressed) {
                    ignore_buttons_until_release = false;
                }
            }
        }

        if (handle_buttons && menu_storage_is_ready()) {
            menu_controller_handle_event(event);

            if (menu_display_is_available() &&
                menu_display_is_awake() &&
                event != MENU_INPUT_EVENT_NONE) {
                menu_display_note_activity();
            }
        }

        if (menu_storage_is_ready()) {
            menu_controller_poll(!menu_display_is_available() ||
                                 menu_display_is_awake());
        }

        if (menu_display_poll_updates()) {
            if (menu_input_any_pressed()) {
                menu_input_discard_current_press();
                ignore_buttons_until_release = true;
            }
        }

        if (menu_display_poll_idle()) {
            ignore_buttons_until_release = false;
        }

        busy_wait_ms(MENU_POLL_MS);
    }
}
