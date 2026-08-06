#include "menu.h"

#include "hardware_config.h"
#include "micro_sd.h"
#include "oled.h"
#include "ps1_card_bus.h"
#include "ps1_card_emulator.h"

#include "pico/stdlib.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MENU_POLL_MS          10u
#define MENU_DEBOUNCE_MS      60u
#define MENU_LONG_PRESS_MS    700u
#define MENU_MESSAGE_MS       1400u
#define MENU_DISPLAY_IDLE_MS  30000u
#define MENU_CARD_DEBOUNCE_MS 100u
#define MENU_CARD_RETRY_MS    1000u
#define MENU_CARD_PROBE_MS    1000u

typedef enum {
    BUTTON_EVENT_NONE,
    BUTTON_EVENT_NEXT_SHORT,
    BUTTON_EVENT_NEXT_LONG,
    BUTTON_EVENT_SELECT_SHORT,
    BUTTON_EVENT_SELECT_LONG,
} button_event_t;

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

typedef struct {
    uint pin;
    bool raw_pressed;
    bool stable_pressed;
    bool long_sent;
    uint32_t raw_changed_ms;
    uint32_t pressed_ms;
} button_state_t;

typedef struct {
    bool raw_present;
    bool stable_present;
    uint32_t raw_changed_ms;
} card_detect_state_t;

static button_state_t next_button = {.pin = MENU_NEXT_PIN};
static button_state_t select_button = {.pin = MENU_SELECT_PIN};

static menu_screen_t screen = MENU_SCREEN_STATUS;
static menu_screen_t message_return_screen = MENU_SCREEN_STATUS;
static uint32_t message_until_ms;

static uint8_t main_index;
static size_t image_count;
static size_t image_index;
static size_t save_count;
static size_t save_index;
static bool confirm_delete_yes;

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

static uint32_t millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool menu_time_reached(uint32_t deadline_ms) {
    return (int32_t)(millis_now() - deadline_ms) >= 0;
}

static void card_detect_state_init(card_detect_state_t *state) {
    if (state == NULL) {
        return;
    }

    state->raw_present = micro_sd_card_present();
    state->stable_present = state->raw_present;
    state->raw_changed_ms = millis_now();
}

static bool card_detect_poll(card_detect_state_t *state, bool *changed) {
    uint32_t now = millis_now();
    bool raw_present = micro_sd_card_present();

    if (changed != NULL) {
        *changed = false;
    }

    if (state == NULL) {
        return raw_present;
    }

    if (raw_present != state->raw_present) {
        state->raw_present = raw_present;
        state->raw_changed_ms = now;
    }

    if (raw_present != state->stable_present &&
        (uint32_t)(now - state->raw_changed_ms) >= MENU_CARD_DEBOUNCE_MS) {
        state->stable_present = raw_present;

        if (changed != NULL) {
            *changed = true;
        }
    }

    return state->stable_present;
}

static void buttons_init(void) {
    gpio_init(MENU_NEXT_PIN);
    gpio_set_dir(MENU_NEXT_PIN, GPIO_IN);
    gpio_pull_up(MENU_NEXT_PIN);

    gpio_init(MENU_SELECT_PIN);
    gpio_set_dir(MENU_SELECT_PIN, GPIO_IN);
    gpio_pull_up(MENU_SELECT_PIN);

    next_button.raw_pressed = !gpio_get(MENU_NEXT_PIN);
    next_button.stable_pressed = next_button.raw_pressed;
    next_button.raw_changed_ms = millis_now();
    next_button.pressed_ms = next_button.raw_changed_ms;

    select_button.raw_pressed = !gpio_get(MENU_SELECT_PIN);
    select_button.stable_pressed = select_button.raw_pressed;
    select_button.raw_changed_ms = next_button.raw_changed_ms;
    select_button.pressed_ms = select_button.raw_changed_ms;
}

static bool buttons_any_pressed(void) {
    return !gpio_get(MENU_NEXT_PIN) || !gpio_get(MENU_SELECT_PIN);
}

static void button_discard_current_press(button_state_t *button) {
    uint32_t now = millis_now();
    bool pressed = !gpio_get(button->pin);

    button->raw_pressed = pressed;
    button->stable_pressed = pressed;
    button->raw_changed_ms = now;
    button->pressed_ms = now;
    button->long_sent = pressed;
}

static void buttons_discard_current_press(void) {
    button_discard_current_press(&next_button);
    button_discard_current_press(&select_button);
}

static button_event_t update_button(button_state_t *button,
                                    button_event_t short_event,
                                    button_event_t long_event) {
    uint32_t now = millis_now();
    bool raw_pressed = !gpio_get(button->pin);

    if (raw_pressed != button->raw_pressed) {
        button->raw_pressed = raw_pressed;
        button->raw_changed_ms = now;
    }

    if (raw_pressed != button->stable_pressed &&
        (uint32_t)(now - button->raw_changed_ms) >= MENU_DEBOUNCE_MS) {
        button->stable_pressed = raw_pressed;

        if (button->stable_pressed) {
            button->pressed_ms = now;
            button->long_sent = false;
        } else if (!button->long_sent) {
            return short_event;
        }
    }

    if (button->stable_pressed &&
        !button->long_sent &&
        (uint32_t)(now - button->pressed_ms) >= MENU_LONG_PRESS_MS) {
        button->long_sent = true;
        return long_event;
    }

    return BUTTON_EVENT_NONE;
}

static button_event_t buttons_poll(void) {
    button_event_t event = update_button(&next_button,
                                         BUTTON_EVENT_NEXT_SHORT,
                                         BUTTON_EVENT_NEXT_LONG);

    if (event != BUTTON_EVENT_NONE) {
        return event;
    }

    return update_button(&select_button,
                         BUTTON_EVENT_SELECT_SHORT,
                         BUTTON_EVENT_SELECT_LONG);
}

static void show_message(menu_screen_t return_screen,
                         const char *line0,
                         const char *line1,
                         const char *line2,
                         const char *line3) {
    screen = MENU_SCREEN_MESSAGE;
    message_return_screen = return_screen;
    message_until_ms = millis_now() + MENU_MESSAGE_MS;
    oled_show_text(line0, line1, line2, line3);
}

static void render_status(void) {
    oled_show_ready_for_image(micro_sd_active_image_name());
}

static void render_no_card(void) {
    screen = MENU_SCREEN_NO_CARD;
    oled_show_text("MICROSD CARD",
                   "NOT INSERTED",
                   "INSERT CARD",
                   "");
}

static void render_card_error(void) {
    screen = MENU_SCREEN_CARD_ERROR;
    oled_show_text("MICROSD ERROR",
                   "CHECK CARD",
                   "REINSERT CARD",
                   "");
}

static void render_main(void) {
    char item_line[22];

    (void)snprintf(item_line,
                   sizeof(item_line),
                   "> %s",
                   main_items[main_index]);

    oled_show_text("MENU",
                   item_line,
                   "L.B. NEXT R.B. OK",
                   "HOLD R.B. BACK");
}

static void find_active_image_index(void) {
    image_index = 0;

    for (size_t i = 0; i < image_count; ++i) {
        if (micro_sd_is_active_image(images[i].name)) {
            image_index = i;
            return;
        }
    }
}

static void render_image_browser(const char *title) {
    char index_line[22];
    const char *status = "";

    if (image_count == 0u) {
        oled_show_text(title,
                       "NO IMAGES",
                       "",
                       "HOLD R.B. BACK");
        return;
    }

    (void)snprintf(index_line,
                   sizeof(index_line),
                   "%02u/%02u %s",
                   (unsigned)(image_index + 1u),
                   (unsigned)image_count,
                   images[image_index].name);

    if (micro_sd_is_active_image(images[image_index].name)) {
        status = "ACTIVE";
    }

    oled_show_text(title,
                   index_line,
                   status,
                   "L.B. NEXT R.B. OK");
}

static void enter_select_image(void) {
    image_count = micro_sd_list_images(images, MICRO_SD_MAX_IMAGES);
    find_active_image_index();
    screen = MENU_SCREEN_SELECT_IMAGE;
    render_image_browser("SELECT IMAGE");
}

static void render_saves(void) {
    char count_line[22];
    char detail_line[22];

    if (save_count == 0u) {
        oled_show_text("SAVES",
                       "NO SAVES",
                       micro_sd_active_image_name(),
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
                   (unsigned)saves[save_index].slot,
                   (unsigned)saves[save_index].blocks);

    oled_show_text(count_line,
                   saves[save_index].file_name,
                   detail_line,
                   "HOLD R.B. BACK");
}

static void enter_saves(void) {
    (void)micro_sd_save_worker_flush();
    save_count = micro_sd_list_saves(micro_sd_active_image_name(),
                                     saves,
                                     MICRO_SD_MAX_SAVES);
    save_index = 0;
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
    oled_show_text("DELETE IMAGE",
                   delete_candidate,
                   confirm_delete_yes ? "CONFIRM: YES" : "CONFIRM: NO",
                   "L.B. TOGGLE R.B. OK");
}

static void create_new_image(void) {
    char new_name[MICRO_SD_IMAGE_NAME_MAX];

    oled_show_text("CREATING IMAGE",
                   "PLEASE WAIT",
                   "",
                   "");

    (void)micro_sd_save_worker_flush();

    if (!micro_sd_create_blank_image_auto(new_name)) {
        show_message(MENU_SCREEN_MAIN,
                     "CREATE FAILED",
                     "CHECK MICROSD",
                     "",
                     "");
        return;
    }

    oled_show_text("LOADING IMAGE",
                   new_name,
                   "PLEASE WAIT",
                   "");

    if (!micro_sd_activate_image_as_inserted_card(new_name)) {
        show_message(MENU_SCREEN_MAIN,
                     "CREATED",
                     new_name,
                     "LOAD FAILED",
                     "");
        return;
    }

    show_message(MENU_SCREEN_STATUS,
                 "CREATED",
                 new_name,
                 "NOW ACTIVE",
                 "");
}

static void select_current_image(void) {
    if (image_count == 0u) {
        return;
    }

    oled_show_text("LOADING IMAGE",
                   images[image_index].name,
                   "PLEASE WAIT",
                   "");

    if (micro_sd_activate_image_as_inserted_card(images[image_index].name)) {
        show_message(MENU_SCREEN_STATUS,
                     "SELECTED",
                     images[image_index].name,
                     "",
                     "");
    } else {
        show_message(MENU_SCREEN_SELECT_IMAGE,
                     "LOAD FAILED",
                     images[image_index].name,
                     "CHECK IMAGE",
                     "");
    }
}

static void delete_current_image(void) {
    oled_show_text("DELETING IMAGE",
                   delete_candidate,
                   "PLEASE WAIT",
                   "");

    if (micro_sd_delete_image(delete_candidate)) {
        show_message(MENU_SCREEN_STATUS,
                     "DELETED",
                     delete_candidate,
                     "ACTIVE:",
                     micro_sd_active_image_name());
    } else {
        show_message(MENU_SCREEN_DELETE_IMAGE,
                     "DELETE FAILED",
                     delete_candidate,
                     "CHECK MICROSD",
                     "");
    }
}

static void render_current_screen(void) {
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
            render_no_card();
            break;
        case MENU_SCREEN_CARD_ERROR:
            render_card_error();
            break;
        default:
            break;
    }
}

static bool reload_inserted_card(const char *initial_image_path) {
    ps1_bus_set_card_present(false);

    if (!micro_sd_card_present()) {
        micro_sd_handle_card_unavailable();
        render_no_card();
        return false;
    }

    if (!micro_sd_load_or_create_initial_image(initial_image_path)) {
        micro_sd_handle_card_unavailable();
        render_card_error();
        return false;
    }

    ps1emu_storage_state_init();
    micro_sd_save_worker_init(micro_sd_active_image_path());
    ps1_bus_begin_card_swap_absent();
    ps1_bus_set_card_present(true);
    micro_sd_clear_card_removed_event();
    screen = MENU_SCREEN_STATUS;
    render_status();
    return true;
}

static void handle_card_removed(card_detect_state_t *card_detect,
                                bool oled_ok,
                                bool *display_awake,
                                bool *card_ready,
                                uint32_t *next_card_retry_ms,
                                uint32_t *last_button_activity_ms) {
    uint32_t now = millis_now();

    if (card_detect != NULL) {
        card_detect->raw_present = false;
        card_detect->stable_present = false;
        card_detect->raw_changed_ms = now;
    }

    if (display_awake != NULL && oled_ok && !*display_awake) {
        oled_set_display_enabled(true);
        *display_awake = true;
    }

    if (card_ready != NULL) {
        *card_ready = false;
    }

    if (next_card_retry_ms != NULL) {
        *next_card_retry_ms = 0u;
    }

    if (last_button_activity_ms != NULL) {
        *last_button_activity_ms = now;
    }

    micro_sd_handle_card_unavailable();
    render_no_card();
}

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

static void handle_event(button_event_t event) {
    if (event == BUTTON_EVENT_NONE) {
        return;
    }

    if (screen == MENU_SCREEN_MESSAGE) {
        return;
    }

    switch (screen) {
        case MENU_SCREEN_STATUS:
            if (event == BUTTON_EVENT_NEXT_SHORT) {
                enter_saves();
            } else if (event == BUTTON_EVENT_SELECT_SHORT ||
                       event == BUTTON_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_MAIN:
            if (event == BUTTON_EVENT_NEXT_SHORT) {
                main_index = (uint8_t)((main_index + 1u) %
                                       (sizeof(main_items) / sizeof(main_items[0])));
                render_main();
            } else if (event == BUTTON_EVENT_SELECT_SHORT) {
                handle_main_select();
            } else if (event == BUTTON_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_STATUS;
                render_status();
            }
            break;

        case MENU_SCREEN_SELECT_IMAGE:
            if (event == BUTTON_EVENT_NEXT_SHORT && image_count > 0u) {
                image_index = (image_index + 1u) % image_count;
                render_image_browser("SELECT IMAGE");
            } else if (event == BUTTON_EVENT_SELECT_SHORT) {
                select_current_image();
            } else if (event == BUTTON_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_SAVES:
            if (event == BUTTON_EVENT_NEXT_SHORT && save_count > 0u) {
                save_index = (save_index + 1u) % save_count;
                render_saves();
            } else if (event == BUTTON_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_DELETE_IMAGE:
            if (event == BUTTON_EVENT_NEXT_SHORT && image_count > 0u) {
                image_index = (image_index + 1u) % image_count;
                render_image_browser("DELETE IMAGE");
            } else if (event == BUTTON_EVENT_SELECT_SHORT && image_count > 0u) {
                (void)snprintf(delete_candidate,
                               sizeof(delete_candidate),
                               "%s",
                               images[image_index].name);
                confirm_delete_yes = false;
                screen = MENU_SCREEN_DELETE_CONFIRM;
                render_delete_confirm();
            } else if (event == BUTTON_EVENT_SELECT_LONG) {
                screen = MENU_SCREEN_MAIN;
                render_main();
            }
            break;

        case MENU_SCREEN_DELETE_CONFIRM:
            if (event == BUTTON_EVENT_NEXT_SHORT) {
                confirm_delete_yes = !confirm_delete_yes;
                render_delete_confirm();
            } else if (event == BUTTON_EVENT_SELECT_SHORT) {
                if (confirm_delete_yes) {
                    delete_current_image();
                } else {
                    screen = MENU_SCREEN_DELETE_IMAGE;
                    render_image_browser("DELETE IMAGE");
                }
            } else if (event == BUTTON_EVENT_SELECT_LONG) {
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

void menu_task_run(const char *initial_image_path) {
    bool oled_ok;
    bool display_awake = false;
    bool ignore_buttons_until_release = false;
    bool card_ready = false;
    card_detect_state_t card_detect;
    uint32_t last_button_activity_ms = millis_now();
    uint32_t next_card_retry_ms = 0u;
    uint32_t next_card_probe_ms = 0u;
    uint32_t last_oled_update_count = 0u;

    buttons_init();
    micro_sd_card_detect_init();
    card_detect_state_init(&card_detect);
    micro_sd_save_worker_init(initial_image_path);
    ps1_bus_set_card_present(false);

    oled_ok = oled_init();

    if (oled_ok) {
        display_awake = true;
        last_button_activity_ms = millis_now();
        last_oled_update_count = oled_get_update_count();
    } else {
        printf("OLED initialization failed\n");
    }

    if (card_detect.stable_present) {
        card_ready = reload_inserted_card(initial_image_path);

        if (!card_ready) {
            next_card_retry_ms = millis_now() + MENU_CARD_RETRY_MS;
        }
    } else {
        micro_sd_handle_card_unavailable();
        render_no_card();
    }

    while (true) {
        bool card_changed = false;
        bool raw_card_present = micro_sd_card_present();
        bool card_removed = micro_sd_card_removed_event();
        bool card_present;

        if (card_ready && (!raw_card_present || card_removed)) {
            handle_card_removed(&card_detect,
                                oled_ok,
                                &display_awake,
                                &card_ready,
                                &next_card_retry_ms,
                                &last_button_activity_ms);
        }

        card_present = card_detect_poll(&card_detect, &card_changed);

        if (card_changed) {
            last_button_activity_ms = millis_now();

            if (oled_ok && !display_awake) {
                oled_set_display_enabled(true);
                display_awake = true;
            }

            if (card_present) {
                card_ready = reload_inserted_card(initial_image_path);
                next_card_retry_ms = card_ready
                        ? 0u
                        : millis_now() + MENU_CARD_RETRY_MS;
            } else {
                handle_card_removed(&card_detect,
                                    oled_ok,
                                    &display_awake,
                                    &card_ready,
                                    &next_card_retry_ms,
                                    &last_button_activity_ms);
            }
        } else if (card_present &&
                   !card_ready &&
                   menu_time_reached(next_card_retry_ms)) {
            card_ready = reload_inserted_card(initial_image_path);
            next_card_retry_ms = card_ready
                    ? 0u
                    : millis_now() + MENU_CARD_RETRY_MS;

            if (card_ready && oled_ok && !display_awake) {
                oled_set_display_enabled(true);
                display_awake = true;
                last_button_activity_ms = millis_now();
            }
        }

        if (card_ready) {
            if (menu_time_reached(next_card_probe_ms)) {
                next_card_probe_ms = millis_now() + MENU_CARD_PROBE_MS;

                if (!micro_sd_active_image_accessible()) {
                    if (micro_sd_card_present()) {
                        card_ready = false;
                        next_card_retry_ms = millis_now() + MENU_CARD_RETRY_MS;
                        micro_sd_handle_card_unavailable();
                        render_card_error();
                    } else {
                        handle_card_removed(&card_detect,
                                            oled_ok,
                                            &display_awake,
                                            &card_ready,
                                            &next_card_retry_ms,
                                            &last_button_activity_ms);
                    }
                }
            }

            if (card_ready) {
                micro_sd_save_worker_poll();

                if (!micro_sd_storage_ready()) {
                    if (micro_sd_card_present()) {
                        card_ready = false;
                        next_card_retry_ms = millis_now() + MENU_CARD_RETRY_MS;
                        render_card_error();
                    } else {
                        handle_card_removed(&card_detect,
                                            oled_ok,
                                            &display_awake,
                                            &card_ready,
                                            &next_card_retry_ms,
                                            &last_button_activity_ms);
                    }
                }
            }
        }

        button_event_t event = buttons_poll();
        bool button_pressed = buttons_any_pressed();
        bool button_activity = button_pressed || event != BUTTON_EVENT_NONE;
        bool handle_buttons = card_ready;

        if (oled_ok) {
            if (button_activity) {
                last_button_activity_ms = millis_now();

                if (!display_awake) {
                    oled_set_display_enabled(true);
                    display_awake = true;
                    buttons_discard_current_press();
                    event = BUTTON_EVENT_NONE;
                    button_pressed = buttons_any_pressed();
                    ignore_buttons_until_release = true;
                    render_current_screen();
                }
            }

            if (!display_awake) {
                handle_buttons = false;
            } else if (ignore_buttons_until_release) {
                handle_buttons = false;

                if (!button_pressed) {
                    ignore_buttons_until_release = false;
                }
            }
        }

        if (handle_buttons && card_ready) {
            handle_event(event);

            if (oled_ok && display_awake && event != BUTTON_EVENT_NONE) {
                last_button_activity_ms = millis_now();
            }
        }

        if (card_ready &&
            screen == MENU_SCREEN_MESSAGE &&
            menu_time_reached(message_until_ms)) {
            screen = message_return_screen;

            if (!oled_ok || display_awake) {
                render_current_screen();
            }
        }

        if (oled_ok) {
            uint32_t oled_update_count = oled_get_update_count();

            if (oled_update_count != last_oled_update_count) {
                last_oled_update_count = oled_update_count;
                last_button_activity_ms = millis_now();

                if (!display_awake) {
                    oled_set_display_enabled(true);
                    display_awake = true;

                    if (buttons_any_pressed()) {
                        buttons_discard_current_press();
                        ignore_buttons_until_release = true;
                    }
                }
            }
        }

        if (oled_ok &&
            display_awake &&
            (uint32_t)(millis_now() - last_button_activity_ms) >= MENU_DISPLAY_IDLE_MS) {
            oled_set_display_enabled(false);
            display_awake = false;
            ignore_buttons_until_release = false;
        }

        busy_wait_ms(MENU_POLL_MS);
    }
}
