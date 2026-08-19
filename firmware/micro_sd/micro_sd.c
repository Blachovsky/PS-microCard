#include "micro_sd/micro_sd.h"

#include "micro_sd/micro_sd_internal.h"

#include "logger/app_log.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"

#include "diskio.h"
#include "ff.h"
#include "f_util.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "micro_sd"

static FATFS fs;
static bool fs_mounted;
static bool card_detect_initialized;
static micro_sd_result_t storage_result = MICRO_SD_ERROR_CARD_NOT_PRESENT;
static volatile bool card_removed_latched;

static char active_image_path[MICRO_SD_IMAGE_PATH_MAX] = "0:/CARD000.MCR";
static char active_image_name[MICRO_SD_IMAGE_NAME_MAX] = "CARD000.MCR";

/* Error reporting. */

const char *micro_sd_result_string(micro_sd_result_t result) {
    switch (result) {
        case MICRO_SD_RESULT_OK:
            return "OK";
        case MICRO_SD_ERROR_INVALID_ARGUMENT:
            return "INVALID ARGUMENT";
        case MICRO_SD_ERROR_CARD_NOT_PRESENT:
            return "CARD NOT PRESENT";
        case MICRO_SD_ERROR_MOUNT_FAILED:
            return "MOUNT FAILED";
        case MICRO_SD_ERROR_STAT_FAILED:
            return "STAT FAILED";
        case MICRO_SD_ERROR_FILE_NOT_FOUND:
            return "FILE NOT FOUND";
        case MICRO_SD_ERROR_INVALID_IMAGE_SIZE:
            return "BAD IMAGE SIZE";
        case MICRO_SD_ERROR_INVALID_IMAGE_FORMAT:
            return "BAD IMAGE FORMAT";
        case MICRO_SD_ERROR_OPEN_FAILED:
            return "OPEN FAILED";
        case MICRO_SD_ERROR_READ_FAILED:
            return "READ FAILED";
        case MICRO_SD_ERROR_SEEK_FAILED:
            return "SEEK FAILED";
        case MICRO_SD_ERROR_WRITE_FAILED:
            return "WRITE FAILED";
        case MICRO_SD_ERROR_SYNC_FAILED:
            return "SYNC FAILED";
        case MICRO_SD_ERROR_CLOSE_FAILED:
            return "CLOSE FAILED";
        case MICRO_SD_ERROR_DELETE_FAILED:
            return "DELETE FAILED";
        case MICRO_SD_ERROR_NO_FREE_IMAGE_NAME:
            return "NO FREE IMAGE NAME";
        case MICRO_SD_ERROR_FRAME_FETCH_FAILED:
            return "FRAME FETCH FAILED";
        default:
            return "UNKNOWN ERROR";
    }
}

/* Card-detect GPIO and public presence state. */

static bool card_detect_pin_present(void) {
    return gpio_get(SD_DETECT_PIN) == SD_DETECT_PRESENT_LEVEL;
}

static void card_detect_gpio_callback(uint gpio, uint32_t events) {
    (void)events;

    if (gpio != SD_DETECT_PIN) {
        return;
    }

    card_removed_latched = !card_detect_pin_present();
}

void micro_sd_card_detect_init(void) {
    gpio_init(SD_DETECT_PIN);
    gpio_set_dir(SD_DETECT_PIN, GPIO_IN);
    gpio_pull_up(SD_DETECT_PIN);
    card_detect_initialized = true;
    card_removed_latched = !card_detect_pin_present();
    gpio_set_irq_enabled_with_callback(SD_DETECT_PIN,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true,
                                       &card_detect_gpio_callback);
}

bool micro_sd_card_present(void) {
    if (!card_detect_initialized) {
        micro_sd_card_detect_init();
    }

    gpio_pull_up(SD_DETECT_PIN);
    return card_detect_pin_present();
}

bool micro_sd_card_removed_event(void) {
    if (!micro_sd_card_present()) {
        card_removed_latched = true;
    }

    return card_removed_latched;
}

void micro_sd_clear_card_removed_event(void) {
    card_removed_latched = false;
}

/* Shared path and name helpers used by image management. */

char micro_sd_internal_ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    return c;
}

bool micro_sd_internal_names_equal_ignore_case(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (micro_sd_internal_ascii_upper(*a++) != micro_sd_internal_ascii_upper(*b++)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

void micro_sd_internal_copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        src = "";
    }

    size_t length = strlen(src);
    size_t copy_length = length < dst_size - 1u
            ? length
            : dst_size - 1u;

    /* The active path may be passed back as the source during reinit. */
    memmove(dst, src, copy_length);
    dst[copy_length] = '\0';
}

void micro_sd_internal_copy_name_from_path(const char *path, char *name, size_t name_size) {
    const char *base = path;

    if (path == NULL) {
        micro_sd_internal_copy_string(name, name_size, "");
        return;
    }

    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            base = p + 1;
        }
    }

    micro_sd_internal_copy_string(name, name_size, base);
}

void micro_sd_internal_name_to_path(const char *name,
                                  char *path,
                                  size_t path_size) {
    if (path == NULL || path_size == 0u) {
        return;
    }

    if (name == NULL) {
        name = "";
    }

    if (strstr(name, ":/") != NULL || strstr(name, ":\\") != NULL) {
        micro_sd_internal_copy_string(path, path_size, name);
    } else {
        (void)snprintf(path, path_size, "0:/%s", name);
    }
}

void micro_sd_internal_set_active_image_path(const char *path) {
    micro_sd_internal_copy_string(active_image_path, sizeof(active_image_path), path);
    micro_sd_internal_copy_name_from_path(active_image_path,
                        active_image_name,
                        sizeof(active_image_name));
}

/* Active image state. */

const char *micro_sd_active_image_path(void) {
    return active_image_path;
}

const char *micro_sd_active_image_name(void) {
    return active_image_name;
}

bool micro_sd_is_active_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];

    micro_sd_internal_copy_name_from_path(image_name, name, sizeof(name));
    return micro_sd_internal_names_equal_ignore_case(name, active_image_name);
}

/* FatFs mount state. */

micro_sd_result_t micro_sd_internal_mount(void) {
    if (!micro_sd_card_present()) {
        fs_mounted = false;
        (void)f_mount(NULL, "0:", 0);
        return MICRO_SD_ERROR_CARD_NOT_PRESENT;
    }

    if (fs_mounted) {
        return MICRO_SD_RESULT_OK;
    }

    FRESULT fr = f_mount(&fs, "0:", 1);

    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_mount failed: %s (%d)",
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_MOUNT_FAILED;
    }

    fs_mounted = true;
    return MICRO_SD_RESULT_OK;
}

micro_sd_result_t micro_sd_internal_storage_result(void) {
    return storage_result;
}

void micro_sd_internal_set_storage_result(micro_sd_result_t result) {
    storage_result = result;
}

void micro_sd_internal_reset_fatfs_card_state(void) {
    fs_mounted = false;
    (void)f_mount(NULL, "0:", 0);

    sd_card_t *sd_card = sd_get_by_num(0);
    if (sd_card != NULL) {
        sd_card->state.m_Status |= STA_NOINIT;
        sd_card->state.card_type = SDCARD_NONE;
        sd_card->state.mounted = false;

        if (micro_sd_card_present()) {
            sd_card->state.m_Status &= (DSTATUS)~STA_NODISK;
        } else {
            sd_card->state.m_Status |= STA_NODISK;
        }
    }
}

/* Card removal recovery shared by the menu and save worker. */

void micro_sd_handle_card_unavailable(void) {
    storage_result = MICRO_SD_ERROR_CARD_NOT_PRESENT;
    ps1_bus_set_card_present(false);
    ps1emu_rollback_unconfirmed_frames();
    micro_sd_internal_worker_reset();
    micro_sd_internal_reset_fatfs_card_state();
}
