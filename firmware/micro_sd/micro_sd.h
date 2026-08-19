#ifndef MICRO_SD_H
#define MICRO_SD_H

#include "board/hardware_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MICRO_SD_IMAGE_NAME_MAX 13u
#define MICRO_SD_IMAGE_PATH_MAX 16u
#define MICRO_SD_MAX_IMAGES     64u
#define MICRO_SD_MAX_SAVES      15u

typedef struct {
    char name[MICRO_SD_IMAGE_NAME_MAX];
} micro_sd_image_entry_t;

typedef struct {
    char file_name[21];
    uint8_t slot;
    uint8_t blocks;
    uint32_t size_bytes;
} micro_sd_save_entry_t;

typedef enum {
    MICRO_SD_RESULT_OK = 0,
    MICRO_SD_ERROR_INVALID_ARGUMENT,
    MICRO_SD_ERROR_CARD_NOT_PRESENT,
    MICRO_SD_ERROR_MOUNT_FAILED,
    MICRO_SD_ERROR_STAT_FAILED,
    MICRO_SD_ERROR_FILE_NOT_FOUND,
    MICRO_SD_ERROR_INVALID_IMAGE_SIZE,
    MICRO_SD_ERROR_INVALID_IMAGE_FORMAT,
    MICRO_SD_ERROR_OPEN_FAILED,
    MICRO_SD_ERROR_READ_FAILED,
    MICRO_SD_ERROR_SEEK_FAILED,
    MICRO_SD_ERROR_WRITE_FAILED,
    MICRO_SD_ERROR_SYNC_FAILED,
    MICRO_SD_ERROR_CLOSE_FAILED,
    MICRO_SD_ERROR_DELETE_FAILED,
    MICRO_SD_ERROR_NO_FREE_IMAGE_NAME,
    MICRO_SD_ERROR_FRAME_FETCH_FAILED,
} micro_sd_result_t;

/* Result reporting and card lifecycle. */
const char *micro_sd_result_string(micro_sd_result_t result);

void micro_sd_card_detect_init(void);
bool micro_sd_card_present(void);
bool micro_sd_card_removed_event(void);
void micro_sd_clear_card_removed_event(void);
void micro_sd_handle_card_unavailable(void);
micro_sd_result_t micro_sd_load_or_create_initial_image(const char *path);
micro_sd_result_t micro_sd_check_active_image_accessible(void);

/* Active image state. */
const char *micro_sd_active_image_path(void);
const char *micro_sd_active_image_name(void);
bool micro_sd_is_active_image(const char *image_name);

/*
 * Step-based FatFs worker for core 1. menu_task_run() calls poll often enough
 * to keep PS1 frame writes synced while still reading buttons.
 */
void micro_sd_save_worker_init(const char *path);
micro_sd_result_t micro_sd_save_worker_poll(void);
micro_sd_result_t micro_sd_save_worker_flush(void);

/* Image catalog and PS1 save metadata. */
micro_sd_result_t micro_sd_create_blank_image_auto(
        char out_name[MICRO_SD_IMAGE_NAME_MAX]);
size_t micro_sd_list_images(micro_sd_image_entry_t *entries, size_t max_entries);
size_t micro_sd_list_saves(const char *image_name,
                           micro_sd_save_entry_t *entries,
                           size_t max_entries);
micro_sd_result_t micro_sd_activate_image_as_inserted_card(
        const char *image_name);
micro_sd_result_t micro_sd_delete_image(const char *image_name);

#endif // MICRO_SD_H
