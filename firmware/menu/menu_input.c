#include "menu/menu_input.h"

#include "board/hardware_config.h"

#include "pico/stdlib.h"

#include <stdint.h>

#define MENU_INPUT_DEBOUNCE_MS   60u
#define MENU_INPUT_LONG_PRESS_MS 700u

typedef struct {
    uint pin;
    bool raw_pressed;
    bool stable_pressed;
    bool long_sent;
    uint32_t raw_changed_ms;
    uint32_t pressed_ms;
} menu_input_button_t;

static menu_input_button_t next_button = {.pin = MENU_NEXT_PIN};
static menu_input_button_t select_button = {.pin = MENU_SELECT_PIN};

static uint32_t menu_input_millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void menu_input_button_init(menu_input_button_t *button,
                                   uint32_t now) {
    gpio_init(button->pin);
    gpio_set_dir(button->pin, GPIO_IN);
    gpio_pull_up(button->pin);

    button->raw_pressed = !gpio_get(button->pin);
    button->stable_pressed = button->raw_pressed;
    button->long_sent = false;
    button->raw_changed_ms = now;
    button->pressed_ms = now;
}

void menu_input_init(void) {
    uint32_t now = menu_input_millis_now();

    menu_input_button_init(&next_button, now);
    menu_input_button_init(&select_button, now);
}

bool menu_input_any_pressed(void) {
    return !gpio_get(MENU_NEXT_PIN) || !gpio_get(MENU_SELECT_PIN);
}

static void menu_input_button_discard_current_press(
        menu_input_button_t *button,
        uint32_t now) {
    bool pressed = !gpio_get(button->pin);

    button->raw_pressed = pressed;
    button->stable_pressed = pressed;
    button->raw_changed_ms = now;
    button->pressed_ms = now;
    button->long_sent = pressed;
}

void menu_input_discard_current_press(void) {
    uint32_t now = menu_input_millis_now();

    menu_input_button_discard_current_press(&next_button, now);
    menu_input_button_discard_current_press(&select_button, now);
}

static menu_input_event_t menu_input_update_button(
        menu_input_button_t *button,
        menu_input_event_t short_event,
        menu_input_event_t long_event) {
    uint32_t now = menu_input_millis_now();
    bool raw_pressed = !gpio_get(button->pin);

    if (raw_pressed != button->raw_pressed) {
        button->raw_pressed = raw_pressed;
        button->raw_changed_ms = now;
    }

    if (raw_pressed != button->stable_pressed &&
        (uint32_t)(now - button->raw_changed_ms) >=
                MENU_INPUT_DEBOUNCE_MS) {
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
        (uint32_t)(now - button->pressed_ms) >=
                MENU_INPUT_LONG_PRESS_MS) {
        button->long_sent = true;
        return long_event;
    }

    return MENU_INPUT_EVENT_NONE;
}

menu_input_event_t menu_input_poll(void) {
    menu_input_event_t event = menu_input_update_button(
            &next_button,
            MENU_INPUT_EVENT_NEXT_SHORT,
            MENU_INPUT_EVENT_NEXT_LONG);

    if (event != MENU_INPUT_EVENT_NONE) {
        return event;
    }

    return menu_input_update_button(&select_button,
                                    MENU_INPUT_EVENT_SELECT_SHORT,
                                    MENU_INPUT_EVENT_SELECT_LONG);
}
