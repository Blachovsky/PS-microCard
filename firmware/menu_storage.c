#include "menu_storage.h"

#include "pico/stdlib.h"

#include <stdint.h>

#define MENU_STORAGE_DEBOUNCE_MS 100u
#define MENU_STORAGE_RETRY_MS    1000u
#define MENU_STORAGE_PROBE_MS    1000u

typedef struct {
    bool raw_present;
    bool stable_present;
    bool ready;
    uint32_t raw_changed_ms;
    uint32_t retry_at_ms;
    uint32_t probe_at_ms;
} menu_storage_state_t;

static menu_storage_state_t storage_state;

/* Card monitoring and scheduling. */

static uint32_t menu_storage_millis_now(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool menu_storage_time_reached(uint32_t now, uint32_t deadline_ms) {
    return (int32_t)(now - deadline_ms) >= 0;
}

void menu_storage_init(void) {
    micro_sd_card_detect_init();

    storage_state.raw_present = micro_sd_card_present();
    storage_state.stable_present = storage_state.raw_present;
    storage_state.ready = false;
    storage_state.raw_changed_ms = menu_storage_millis_now();
    storage_state.retry_at_ms = 0u;
    storage_state.probe_at_ms = 0u;
}

bool menu_storage_is_present(void) {
    return storage_state.stable_present;
}

bool menu_storage_is_ready(void) {
    return storage_state.ready;
}

bool menu_storage_is_physically_present(void) {
    return micro_sd_card_present();
}

void menu_storage_mark_ready(void) {
    storage_state.ready = true;
    storage_state.retry_at_ms = 0u;
    micro_sd_clear_card_removed_event();
}

void menu_storage_mark_error(void) {
    storage_state.ready = false;
    storage_state.retry_at_ms = menu_storage_millis_now() +
            MENU_STORAGE_RETRY_MS;
}

void menu_storage_mark_removed(void) {
    uint32_t now = menu_storage_millis_now();

    storage_state.raw_present = false;
    storage_state.stable_present = false;
    storage_state.ready = false;
    storage_state.raw_changed_ms = now;
    storage_state.retry_at_ms = 0u;
}

menu_storage_event_t menu_storage_poll(void) {
    uint32_t now = menu_storage_millis_now();
    bool raw_present = micro_sd_card_present();
    bool removal_latched = micro_sd_card_removed_event();

    if (storage_state.ready && (!raw_present || removal_latched)) {
        menu_storage_mark_removed();
        return MENU_STORAGE_EVENT_REMOVED;
    }

    if (raw_present != storage_state.raw_present) {
        storage_state.raw_present = raw_present;
        storage_state.raw_changed_ms = now;
    }

    if (raw_present != storage_state.stable_present &&
        (uint32_t)(now - storage_state.raw_changed_ms) >=
                MENU_STORAGE_DEBOUNCE_MS) {
        storage_state.stable_present = raw_present;

        if (raw_present) {
            return MENU_STORAGE_EVENT_INSERTED;
        }

        menu_storage_mark_removed();
        return MENU_STORAGE_EVENT_REMOVED;
    }

    if (storage_state.stable_present &&
        !storage_state.ready &&
        menu_storage_time_reached(now, storage_state.retry_at_ms)) {
        return MENU_STORAGE_EVENT_RETRY_DUE;
    }

    if (storage_state.ready &&
        menu_storage_time_reached(now, storage_state.probe_at_ms)) {
        storage_state.probe_at_ms = now + MENU_STORAGE_PROBE_MS;
        return MENU_STORAGE_EVENT_PROBE_DUE;
    }

    return MENU_STORAGE_EVENT_NONE;
}

/* Storage operations and structured error results. */

static menu_storage_action_result_t menu_storage_result(
        menu_storage_action_code_t code,
        micro_sd_result_t cause) {
    menu_storage_action_result_t result = {
        .code = code,
        .cause = cause,
    };

    return result;
}

menu_storage_action_result_t menu_storage_prepare_saves(void) {
    micro_sd_result_t result = micro_sd_save_worker_flush();

    if (result != MICRO_SD_RESULT_OK) {
        return menu_storage_result(MENU_STORAGE_ACTION_ERROR_FLUSH, result);
    }

    return menu_storage_result(MENU_STORAGE_ACTION_OK, MICRO_SD_RESULT_OK);
}

menu_storage_action_result_t menu_storage_create_image(
        char out_name[MICRO_SD_IMAGE_NAME_MAX]) {
    micro_sd_result_t result = micro_sd_save_worker_flush();

    if (result != MICRO_SD_RESULT_OK) {
        return menu_storage_result(MENU_STORAGE_ACTION_ERROR_FLUSH, result);
    }

    result = micro_sd_create_blank_image_auto(out_name);
    if (result != MICRO_SD_RESULT_OK) {
        return menu_storage_result(MENU_STORAGE_ACTION_ERROR_CREATE, result);
    }

    return menu_storage_result(MENU_STORAGE_ACTION_OK, MICRO_SD_RESULT_OK);
}

menu_storage_action_result_t menu_storage_activate_image(
        const char *image_name) {
    micro_sd_result_t result = micro_sd_activate_image_as_inserted_card(
            image_name);

    if (result != MICRO_SD_RESULT_OK) {
        return menu_storage_result(MENU_STORAGE_ACTION_ERROR_ACTIVATE, result);
    }

    return menu_storage_result(MENU_STORAGE_ACTION_OK, MICRO_SD_RESULT_OK);
}

menu_storage_action_result_t menu_storage_delete_image(
        const char *image_name) {
    micro_sd_result_t result = micro_sd_delete_image(image_name);

    if (result != MICRO_SD_RESULT_OK) {
        return menu_storage_result(MENU_STORAGE_ACTION_ERROR_DELETE, result);
    }

    return menu_storage_result(MENU_STORAGE_ACTION_OK, MICRO_SD_RESULT_OK);
}
