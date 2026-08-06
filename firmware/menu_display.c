#include "menu_display.h"

#include "pico/stdlib.h"

#include <stdint.h>

#define MENU_DISPLAY_IDLE_MS 30000u

typedef struct {
    bool available;
    bool awake;
    uint32_t last_activity_ms;
    uint32_t last_update_count;
} menu_display_state_t;

static menu_display_state_t display_state;

static uint32_t menu_display_millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

oled_result_t menu_display_init(void) {
    oled_result_t result = oled_init();

    display_state.available = result == OLED_RESULT_OK;
    display_state.awake = display_state.available;
    display_state.last_activity_ms = menu_display_millis_now();
    display_state.last_update_count = display_state.available
            ? oled_get_update_count()
            : 0u;

    return result;
}

bool menu_display_is_available(void) {
    return display_state.available;
}

bool menu_display_is_awake(void) {
    return display_state.awake;
}

void menu_display_note_activity(void) {
    display_state.last_activity_ms = menu_display_millis_now();
}

bool menu_display_wake(void) {
    menu_display_note_activity();

    if (!display_state.available || display_state.awake) {
        return false;
    }

    oled_set_display_enabled(true);
    display_state.awake = true;
    return true;
}

bool menu_display_poll_updates(void) {
    uint32_t update_count;

    if (!display_state.available) {
        return false;
    }

    update_count = oled_get_update_count();

    if (update_count == display_state.last_update_count) {
        return false;
    }

    display_state.last_update_count = update_count;
    menu_display_note_activity();

    if (display_state.awake) {
        return false;
    }

    oled_set_display_enabled(true);
    display_state.awake = true;
    return true;
}

bool menu_display_poll_idle(void) {
    uint32_t now = menu_display_millis_now();

    if (!display_state.available ||
        !display_state.awake ||
        (uint32_t)(now - display_state.last_activity_ms) <
                MENU_DISPLAY_IDLE_MS) {
        return false;
    }

    oled_set_display_enabled(false);
    display_state.awake = false;
    return true;
}
