#ifndef MENU_STORAGE_H
#define MENU_STORAGE_H

#include "micro_sd/micro_sd.h"

#include <stdbool.h>

/* Card presence, readiness, retry and probe events. */
typedef enum {
    MENU_STORAGE_EVENT_NONE = 0,
    MENU_STORAGE_EVENT_INSERTED,
    MENU_STORAGE_EVENT_REMOVED,
    MENU_STORAGE_EVENT_RETRY_DUE,
    MENU_STORAGE_EVENT_PROBE_DUE,
} menu_storage_event_t;

void menu_storage_init(void);
menu_storage_event_t menu_storage_poll(void);
bool menu_storage_is_present(void);
bool menu_storage_is_ready(void);
bool menu_storage_is_physically_present(void);
void menu_storage_mark_ready(void);
void menu_storage_mark_error(void);
void menu_storage_mark_removed(void);

/* Storage operations used by the menu controller. */
typedef enum {
    MENU_STORAGE_ACTION_OK = 0,
    MENU_STORAGE_ACTION_ERROR_FLUSH,
    MENU_STORAGE_ACTION_ERROR_CREATE,
    MENU_STORAGE_ACTION_ERROR_ACTIVATE,
    MENU_STORAGE_ACTION_ERROR_DELETE,
} menu_storage_action_code_t;

typedef struct {
    menu_storage_action_code_t code;
    micro_sd_result_t cause;
} menu_storage_action_result_t;

menu_storage_action_result_t menu_storage_prepare_saves(void);
menu_storage_action_result_t menu_storage_create_image(
        char out_name[MICRO_SD_IMAGE_NAME_MAX]);
menu_storage_action_result_t menu_storage_activate_image(
        const char *image_name);
menu_storage_action_result_t menu_storage_delete_image(
        const char *image_name);

#endif // MENU_STORAGE_H
