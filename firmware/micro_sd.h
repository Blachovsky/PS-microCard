#ifndef MICRO_SD_H
#define MICRO_SD_H

#include "hardware_config.h"

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

void micro_sd_card_detect_init(void);
bool micro_sd_card_present(void);
bool micro_sd_card_removed_event(void);
void micro_sd_clear_card_removed_event(void);
bool micro_sd_storage_ready(void);
void micro_sd_handle_card_unavailable(void);
bool micro_sd_load_or_create_initial_image(const char *path);
bool micro_sd_active_image_accessible(void);

const char *micro_sd_active_image_path(void);
const char *micro_sd_active_image_name(void);
bool micro_sd_is_active_image(const char *image_name);

/*
 * Step-based FatFs worker for core 1. menu_task_run() calls poll often enough
 * to keep PS1 frame writes synced while still reading buttons.
 */
void micro_sd_save_worker_init(const char *path);
void micro_sd_save_worker_poll(void);
bool micro_sd_save_worker_flush(void);

bool micro_sd_create_blank_image_auto(char out_name[MICRO_SD_IMAGE_NAME_MAX]);
size_t micro_sd_list_images(micro_sd_image_entry_t *entries, size_t max_entries);
size_t micro_sd_list_saves(const char *image_name,
                           micro_sd_save_entry_t *entries,
                           size_t max_entries);
bool micro_sd_activate_image_as_inserted_card(const char *image_name);
bool micro_sd_delete_image(const char *image_name);

#endif // MICRO_SD_H
