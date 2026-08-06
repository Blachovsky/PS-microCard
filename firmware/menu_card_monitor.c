#include "menu_card_monitor.h"

#include "micro_sd.h"

#include "pico/stdlib.h"

#include <stdint.h>

#define MENU_CARD_DEBOUNCE_MS 100u
#define MENU_CARD_RETRY_MS    1000u
#define MENU_CARD_PROBE_MS    1000u

typedef struct {
    bool raw_present;
    bool stable_present;
    bool ready;
    uint32_t raw_changed_ms;
    uint32_t retry_at_ms;
    uint32_t probe_at_ms;
} menu_card_monitor_state_t;

static menu_card_monitor_state_t card_state;

static uint32_t menu_card_millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool menu_card_time_reached(uint32_t now, uint32_t deadline_ms) {
    return (int32_t)(now - deadline_ms) >= 0;
}

void menu_card_monitor_init(void) {
    micro_sd_card_detect_init();

    card_state.raw_present = micro_sd_card_present();
    card_state.stable_present = card_state.raw_present;
    card_state.ready = false;
    card_state.raw_changed_ms = menu_card_millis_now();
    card_state.retry_at_ms = 0u;
    card_state.probe_at_ms = 0u;
}

bool menu_card_monitor_is_present(void) {
    return card_state.stable_present;
}

bool menu_card_monitor_is_ready(void) {
    return card_state.ready;
}

bool menu_card_monitor_is_physically_present(void) {
    return micro_sd_card_present();
}

void menu_card_monitor_mark_ready(void) {
    card_state.ready = true;
    card_state.retry_at_ms = 0u;
    micro_sd_clear_card_removed_event();
}

void menu_card_monitor_mark_error(void) {
    card_state.ready = false;
    card_state.retry_at_ms = menu_card_millis_now() + MENU_CARD_RETRY_MS;
}

void menu_card_monitor_mark_removed(void) {
    uint32_t now = menu_card_millis_now();

    card_state.raw_present = false;
    card_state.stable_present = false;
    card_state.ready = false;
    card_state.raw_changed_ms = now;
    card_state.retry_at_ms = 0u;
}

menu_card_event_t menu_card_monitor_poll(void) {
    uint32_t now = menu_card_millis_now();
    bool raw_present = micro_sd_card_present();
    bool removal_latched = micro_sd_card_removed_event();

    if (card_state.ready && (!raw_present || removal_latched)) {
        menu_card_monitor_mark_removed();
        return MENU_CARD_EVENT_REMOVED;
    }

    if (raw_present != card_state.raw_present) {
        card_state.raw_present = raw_present;
        card_state.raw_changed_ms = now;
    }

    if (raw_present != card_state.stable_present &&
        (uint32_t)(now - card_state.raw_changed_ms) >=
                MENU_CARD_DEBOUNCE_MS) {
        card_state.stable_present = raw_present;

        if (raw_present) {
            return MENU_CARD_EVENT_INSERTED;
        }

        menu_card_monitor_mark_removed();
        return MENU_CARD_EVENT_REMOVED;
    }

    if (card_state.stable_present &&
        !card_state.ready &&
        menu_card_time_reached(now, card_state.retry_at_ms)) {
        return MENU_CARD_EVENT_RETRY_DUE;
    }

    if (card_state.ready &&
        menu_card_time_reached(now, card_state.probe_at_ms)) {
        card_state.probe_at_ms = now + MENU_CARD_PROBE_MS;
        return MENU_CARD_EVENT_PROBE_DUE;
    }

    return MENU_CARD_EVENT_NONE;
}
