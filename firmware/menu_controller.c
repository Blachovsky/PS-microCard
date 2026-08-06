#include "menu_controller.h"

#include "menu_display.h"
#include "menu_storage.h"

#include "pico/stdlib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MENU_MESSAGE_MS 1400u

/* Menu state and screen data. */

typedef enum {
    MENU_SCREEN_STATUS,
    MENU_SCREEN_MAIN,
    MENU_SCREEN_SELECT_IMAGE,
    MENU_SCREEN_SAVES,
    MENU_SCREEN_DELETE_IMAGE,
    MENU_SCREEN_DELETE_CONFIRM,
    MENU_SCREEN_MESSAGE,
    MENU_SCREEN_NO_CARD,
    MENU_SCREEN_CARD_ERROR,
} menu_screen_t;

static menu_screen_t screen;
static menu_screen_t message_return_screen;
static uint32_t message_until_ms;

static uint8_t main_index;
static size_t image_count;
static size_t image_index;
static size_t save_count;
static size_t save_index;
static bool confirm_delete_yes;
static micro_sd_result_t last_card_error;

static micro_sd_image_entry_t images[MICRO_SD_MAX_IMAGES];
static micro_sd_save_entry_t saves[MICRO_SD_MAX_SAVES];
static char delete_candidate[MICRO_SD_IMAGE_NAME_MAX];

static const char *const main_items[] = {
    "SELECT IMAGE",
    "NEW IMAGE",
    "VIEW SAVES",
    "DELETE IMAGE",
    "EXIT",
};

/* Timing and transient messages. */

static uint32_t menu_controller_millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool menu_controller_time_reached(uint32_t deadline_ms) {
    return (int32_t)(menu_controller_millis_now() - deadline_ms) >= 0;
}

static void show_message(menu_screen_t return_screen,
                         const char *line0,
                         const char *line1,
                         const char *line2,
                         const char *line3) {
    screen = MENU_SCREEN_MESSAGE;
    message_return_screen = return_screen;
    message_until_ms = menu_controller_millis_now() + MENU_MESSAGE_MS;
    menu_display_show_message(line0, line1, line2, line3);
}

static void show_micro_sd_error(menu_screen_t return_screen,
                                const char *operation,
                                const char *detail,
                                micro_sd_result_t result) {
    show_message(return_screen,
                 operation,
                 detail,
                 micro_sd_result_string(result),
                 "");
}

/* Screen rendering and list preparation. */

static void render_status(void) {
    menu_display_show_status(micro_sd_active_image_name());
}

static void render_main(void) {
    menu_display_show_main(main_items[main_index]);
}

static void find_active_image_index(void) {
    image_index = 0u;

    for (size_t i = 0u; i < image_count; ++i) {
        if (micro_sd_is_active_image(images[i].name)) {
            image_index = i;
            return;
        }
    }
}

static void render_image_browser(const char *title) {
    if (image_count == 0u) {
        menu_display_show_image_browser(title, "", 0u, 0u, false);
        return;
    }

    menu_display_show_image_browser(
            title,
            images[image_index].name,
            image_index,
            image_count,
            micro_sd_is_active_image(images[image_index].name));
}

static void enter_select_image(void) {
    image_count = micro_sd_list_images(images, MICRO_SD_MAX_IMAGES);
    find_active_image_index();
    screen = MENU_SCREEN_SELECT_IMAGE;
    render_image_browser("SELECT IMAGE");
}

static void render_saves(void) {
    if (save_count == 0u) {
        menu_display_show_saves(micro_sd_active_image_name(),
                                "",
                                0u,
                                0u,
                                0u,
                                0u);
        return;
    }

    menu_display_show_saves(micro_sd_active_image_name(),
                            saves[save_index].file_name,
                            saves[save_index].slot,
                            saves[save_index].blocks,
                            save_index,
                            save_count);
}

static void enter_saves(void) {
    menu_storage_action_result_t result = menu_storage_prepare_saves();

    if (result.code != MENU_STORAGE_ACTION_OK) {
        show_micro_sd_error(MENU_SCREEN_MAIN,
                            "READ SAVES FAILED",
                            micro_sd_active_image_name(),
                            result.cause);
        return;
    }

    save_count = micro_sd_list_saves(micro_sd_active_image_name(),
                                     saves,
                                     MICRO_SD_MAX_SAVES);
    save_index = 0u;
    screen = MENU_SCREEN_SAVES;
    render_saves();
}

static void enter_delete_image(void) {
    image_count = micro_sd_list_images(images, MICRO_SD_MAX_IMAGES);
    find_active_image_index();
    screen = MENU_SCREEN_DELETE_IMAGE;
    render_image_browser("DELETE IMAGE");
}

static void render_delete_confirm(void) {
    menu_display_show_delete_confirm(delete_candidate, confirm_delete_yes);
}

/* Storage-backed user actions. */

static void create_new_image(void) {
    char new_name[MICRO_SD_IMAGE_NAME_MAX];
    menu_storage_action_result_t result;

    menu_display_show_creating_image();

    result = menu_storage_create_image(new_name);
    if (result.code != MENU_STORAGE_ACTION_OK) {
        const char *detail = result.code == MENU_STORAGE_ACTION_ERROR_FLUSH
                ? "FLUSH ACTIVE IMAGE"
                : "CHECK MICROSD";

        show_micro_sd_error(MENU_SCREEN_MAIN,
                            "CREATE FAILED",
                            detail,
                            result.cause);
        return;
    }

    menu_display_show_loading_image(new_name);

    result = menu_storage_activate_image(new_name);
    if (result.code != MENU_STORAGE_ACTION_OK) {
        show_micro_sd_error(MENU_SCREEN_MAIN,
                            "LOAD FAILED",
                            new_name,
                            result.cause);
        return;
    }

    show_message(MENU_SCREEN_STATUS,
                 "CREATED",
                 new_name,
                 "NOW ACTIVE",
                 "");
}

static void select_current_image(void) {
    menu_storage_action_result_t result;

    if (image_count == 0u) {
        return;
    }

    menu_display_show_loading_image(images[image_index].name);

    result = menu_storage_activate_image(images[image_index].name);
    if (result.code == MENU_STORAGE_ACTION_OK) {
        show_message(MENU_SCREEN_STATUS,
                     "SELECTED",
                     images[image_index].name,
                     "",
                     "");
    } else {
        show_micro_sd_error(MENU_SCREEN_SELECT_IMAGE,
                            "LOAD FAILED",
                            images[image_index].name,
                            result.cause);
    }
}

static void delete_current_image(void) {
    menu_storage_action_result_t result;

    menu_display_show_deleting_image(delete_candidate);

    result = menu_storage_delete_image(delete_candidate);
    if (result.code == MENU_STORAGE_ACTION_OK) {
        show_message(MENU_SCREEN_STATUS,
                     "DELETED",
                     delete_candidate,
                     "ACTIVE:",
                     micro_sd_active_image_name());
    } else {
        show_micro_sd_error(MENU_SCREEN_DELETE_IMAGE,
                            "DELETE FAILED",
                            delete_candidate,
                            result.cause);
    }
}

/* Public screen-state API. */

void menu_controller_init(void) {
    screen = MENU_SCREEN_STATUS;
    message_return_screen = MENU_SCREEN_STATUS;
    message_until_ms = 0u;
    main_index = 0u;
    image_count = 0u;
    image_index = 0u;
    save_count = 0u;
    save_index = 0u;
    confirm_delete_yes = false;
    last_card_error = MICRO_SD_RESULT_OK;
    delete_candidate[0] = '\0';
}

void menu_controller_show_status(void) {
    last_card_error = MICRO_SD_RESULT_OK;
    screen = MENU_SCREEN_STATUS;
    render_status();
}

void menu_controller_show_no_card(void) {
    screen = MENU_SCREEN_NO_CARD;
    menu_display_show_no_card();
}

void menu_controller_show_card_error(micro_sd_result_t result) {
    last_card_error = result;
    screen = MENU_SCREEN_CARD_ERROR;
    menu_display_show_card_error(micro_sd_result_string(last_card_error));
}

void menu_controller_render_current(void) {
    switch (screen) {
        case MENU_SCREEN_STATUS:
            render_status();
            break;
        case MENU_SCREEN_MAIN:
            render_main();
            break;
        case MENU_SCREEN_SELECT_IMAGE:
            render_image_browser("SELECT IMAGE");
            break;
        case MENU_SCREEN_SAVES:
            render_saves();
            break;
        case MENU_SCREEN_DELETE_IMAGE:
            render_image_browser("DELETE IMAGE");
            break;
        case MENU_SCREEN_DELETE_CONFIRM:
            render_delete_confirm();
            break;
        case MENU_SCREEN_MESSAGE:
            break;
        case MENU_SCREEN_NO_CARD:
            menu_controller_show_no_card();
            break;
        case MENU_SCREEN_CARD_ERROR:
            menu_display_show_card_error(micro_sd_result_string(last_card_error));
            break;
        default:
            break;
    }
}

/* Navigation and input-event handling. */

static void handle_main_select(void) {
    switch (main_index) {
        case 0:
            enter_select_image();
            break;
        case 1:
            create_new_image();
            break;
        case 2:
            enter_saves();
            break;
        case 3:
            enter_delete_image();
            break;
        case 4:
        default:
            screen = MENU_SCREEN_STATUS;
            render_status();
            break;
    }
}

void menu_controller_handle_event(menu_input_event_t event) {
    if (event == MENU_INPUT_EVENT_NONE || screen == MENU_SCREEN_MESSAGE) {
        return;
    }

    switch (screen) {
        case MENU_SCREEN_STATUS:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT) {
                enter_saves();
            } else if (event == MENU_INPUT_EVENT_SELECT_SHORT ||
                       event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_MAIN:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT) {
                main_index = (uint8_t)((main_index + 1u) %
                                       (sizeof(main_items) / sizeof(main_items[0])));
                render_main();
            } else if (event == MENU_INPUT_EVENT_SELECT_SHORT) {
                handle_main_select();
            } else if (event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_STATUS;
                render_status();
            }
            break;

        case MENU_SCREEN_SELECT_IMAGE:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT && image_count > 0u) {
                image_index = (image_index + 1u) % image_count;
                render_image_browser("SELECT IMAGE");
            } else if (event == MENU_INPUT_EVENT_SELECT_SHORT) {
                select_current_image();
            } else if (event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_SAVES:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT && save_count > 0u) {
                save_index = (save_index + 1u) % save_count;
                render_saves();
            } else if (event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_DELETE_IMAGE:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT && image_count > 0u) {
                image_index = (image_index + 1u) % image_count;
                render_image_browser("DELETE IMAGE");
            } else if (event == MENU_INPUT_EVENT_SELECT_SHORT && image_count > 0u) {
                (void)snprintf(delete_candidate,
                               sizeof(delete_candidate),
                               "%s",
                               images[image_index].name);
                confirm_delete_yes = false;
                screen = MENU_SCREEN_DELETE_CONFIRM;
                render_delete_confirm();
            } else if (event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_DELETE_CONFIRM:
            if (event == MENU_INPUT_EVENT_NEXT_SHORT) {
                confirm_delete_yes = !confirm_delete_yes;
                render_delete_confirm();
            } else if (event == MENU_INPUT_EVENT_SELECT_SHORT) {
                if (confirm_delete_yes) {
                    delete_current_image();
                } else {
                    screen = MENU_SCREEN_DELETE_IMAGE;
                    render_image_browser("DELETE IMAGE");
                }
            } else if (event == MENU_INPUT_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_DELETE_IMAGE;
                render_image_browser("DELETE IMAGE");
            }
            break;

        case MENU_SCREEN_MESSAGE:
        case MENU_SCREEN_NO_CARD:
        case MENU_SCREEN_CARD_ERROR:
        default:
            break;
    }
}

void menu_controller_poll(bool render_enabled) {
    if (screen != MENU_SCREEN_MESSAGE ||
        !menu_controller_time_reached(message_until_ms)) {
        return;
    }

    screen = message_return_screen;

    if (render_enabled) {
        menu_controller_render_current();
    }
}
