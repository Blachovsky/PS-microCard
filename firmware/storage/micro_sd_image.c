#include "storage/micro_sd.h"

#include "storage/micro_sd_internal.h"

#include "logger/app_log.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"

#include "ff.h"
#include "f_util.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "micro_sd"

#define PS1_BLOCK_SIZE        (PS1_FRAME_SIZE * 64u)
#define PS1_DIR_FRAME_COUNT   16u
#define PS1_DIR_ENTRY_COUNT   15u
#define PS1_DIR_NAME_OFFSET   10u
#define PS1_DIR_NAME_SIZE     20u
#define PS1_DIR_CHECKSUM_POS  127u

#define PS1_DIR_FREE          0xA0u
#define PS1_DIR_USED_FIRST    0x51u

/* Image validation and PS1 blank-card formatting. */

static bool has_mcr_extension(const char *name) {
    size_t len;

    if (name == NULL) {
        return false;
    }

    len = strlen(name);
    if (len < 5u) {
        return false;
    }

    const char *ext = &name[len - 4u];
    return ext[0] == '.' &&
           micro_sd_internal_ascii_upper(ext[1]) == 'M' &&
           micro_sd_internal_ascii_upper(ext[2]) == 'C' &&
           micro_sd_internal_ascii_upper(ext[3]) == 'R';
}

static bool stat_valid_image(const char *path, FILINFO *info) {
    FRESULT fr = f_stat(path, info);

    if (fr != FR_OK) {
        return false;
    }

    if ((info->fattrib & AM_DIR) != 0u) {
        return false;
    }

    return info->fsize == PS1_CARD_SIZE;
}

static bool card_image_is_erased_blank(void);
static micro_sd_result_t overwrite_existing_image_with_blank_format(
        const char *path);

static micro_sd_result_t load_card_image_from_sd(const char *path) {
    FIL file;
    FILINFO info;
    FRESULT fr;
    UINT read_bytes;
    micro_sd_result_t result;

    if (path == NULL || path[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    result = micro_sd_internal_mount();
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    fr = f_stat(path, &info);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_stat failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return fr == FR_NO_FILE
                ? MICRO_SD_ERROR_FILE_NOT_FOUND
                : MICRO_SD_ERROR_STAT_FAILED;
    }

    if (info.fsize != PS1_CARD_SIZE) {
        LOG_ERROR(LOG_TAG,
                  "Invalid card image size: path=%s, size=%lu",
                  path,
                  (unsigned long)info.fsize);
        return MICRO_SD_ERROR_INVALID_IMAGE_SIZE;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_open failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_OPEN_FAILED;
    }

    fr = f_read(&file, card_image, PS1_CARD_SIZE, &read_bytes);
    if (fr != FR_OK || read_bytes != PS1_CARD_SIZE) {
        LOG_ERROR(LOG_TAG,
                  "f_read failed: path=%s, error=%s (%d), read=%u",
                  path,
                  FRESULT_str(fr),
                  fr,
                  read_bytes);
        (void)f_close(&file);
        return MICRO_SD_ERROR_READ_FAILED;
    }

    fr = f_close(&file);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_close failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_CLOSE_FAILED;
    }

    if (card_image_is_erased_blank()) {
        LOG_WARNING(LOG_TAG,
                    "Card image is erased blank; formatting: %s",
                    path);

        result = overwrite_existing_image_with_blank_format(path);
        if (result != MICRO_SD_RESULT_OK) {
            return result;
        }
    }

    micro_sd_internal_set_active_image_path(path);
    LOG_INFO(LOG_TAG, "Card image loaded: %s", path);
    return MICRO_SD_RESULT_OK;
}

static uint8_t frame_checksum(const uint8_t frame[PS1_FRAME_SIZE]) {
    uint8_t checksum = 0;

    for (size_t i = 0; i < PS1_DIR_CHECKSUM_POS; ++i) {
        checksum ^= frame[i];
    }

    return checksum;
}

static void make_blank_card_frame(uint16_t frame_addr,
                                  uint8_t frame[PS1_FRAME_SIZE]) {
    memset(frame, 0, PS1_FRAME_SIZE);

    if (frame_addr == 0u) {
        frame[0] = 'M';
        frame[1] = 'C';
        frame[PS1_DIR_CHECKSUM_POS] = frame_checksum(frame);
    } else if (frame_addr < PS1_DIR_FRAME_COUNT) {
        frame[0] = PS1_DIR_FREE;
        frame[8] = 0xFFu;
        frame[9] = 0xFFu;
        frame[PS1_DIR_CHECKSUM_POS] = frame_checksum(frame);
    }
}

static void make_blank_card_image(uint8_t *image) {
    if (image == NULL) {
        return;
    }

    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        make_blank_card_frame(i, &image[(size_t)i * PS1_FRAME_SIZE]);
    }
}

static bool card_image_is_filled_with(uint8_t value) {
    for (size_t i = 0; i < PS1_CARD_SIZE; ++i) {
        if (card_image[i] != value) {
            return false;
        }
    }

    return true;
}

static bool card_image_is_erased_blank(void) {
    return card_image_is_filled_with(0x00u) ||
           card_image_is_filled_with(0xFFu);
}

static micro_sd_result_t overwrite_existing_image_with_blank_format(
        const char *path) {
    FIL file;
    uint8_t frame[PS1_FRAME_SIZE];
    FRESULT fr;

    fr = f_open(&file, path, FA_WRITE | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_open for format failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_OPEN_FAILED;
    }

    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        UINT written = 0;

        make_blank_card_frame(i, frame);
        fr = f_lseek(&file, (FSIZE_t)i * PS1_FRAME_SIZE);

        if (fr != FR_OK) {
            LOG_ERROR(LOG_TAG,
                      "Blank format seek failed: path=%s, frame=%u, "
                      "error=%s (%d)",
                      path,
                      i,
                      FRESULT_str(fr),
                      fr);
            (void)f_close(&file);
            return MICRO_SD_ERROR_SEEK_FAILED;
        }

        fr = f_write(&file, frame, sizeof(frame), &written);
        if (fr != FR_OK || written != sizeof(frame)) {
            LOG_ERROR(LOG_TAG,
                      "Blank format write failed: path=%s, frame=%u, "
                      "error=%s (%d), written=%u",
                      path,
                      i,
                      FRESULT_str(fr),
                      fr,
                      written);
            (void)f_close(&file);
            return MICRO_SD_ERROR_WRITE_FAILED;
        }
    }

    fr = f_sync(&file);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Blank format sync failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        (void)f_close(&file);
        return MICRO_SD_ERROR_SYNC_FAILED;
    }

    fr = f_close(&file);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Blank format close failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_CLOSE_FAILED;
    }

    make_blank_card_image(card_image);
    LOG_INFO(LOG_TAG, "Blank card image formatted: %s", path);
    return MICRO_SD_RESULT_OK;
}

static micro_sd_result_t create_blank_image_at_path(const char *path) {
    FIL file;
    uint8_t frame[PS1_FRAME_SIZE];
    FRESULT fr;
    micro_sd_result_t result;

    if (path == NULL || path[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    result = micro_sd_internal_mount();
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    fr = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_open for create failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_OPEN_FAILED;
    }

    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        UINT written = 0;

        make_blank_card_frame(i, frame);
        fr = f_write(&file, frame, sizeof(frame), &written);

        if (fr != FR_OK || written != sizeof(frame)) {
            LOG_ERROR(LOG_TAG,
                      "Blank image write failed: path=%s, frame=%u, "
                      "error=%s (%d), written=%u",
                      path,
                      i,
                      FRESULT_str(fr),
                      fr,
                      written);
            (void)f_close(&file);
            (void)f_unlink(path);
            return MICRO_SD_ERROR_WRITE_FAILED;
        }
    }

    fr = f_sync(&file);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Blank image sync failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        (void)f_close(&file);
        (void)f_unlink(path);
        return MICRO_SD_ERROR_SYNC_FAILED;
    }

    fr = f_close(&file);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Blank image close failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        (void)f_unlink(path);
        return MICRO_SD_ERROR_CLOSE_FAILED;
    }

    LOG_INFO(LOG_TAG, "Blank card image created: %s", path);
    return MICRO_SD_RESULT_OK;
}

micro_sd_result_t micro_sd_load_or_create_initial_image(const char *path) {
    FILINFO info;
    FRESULT fr;
    micro_sd_result_t result;

    if (path == NULL || path[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    result = micro_sd_internal_mount();
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    fr = f_stat(path, &info);

    if (fr == FR_NO_FILE) {
        LOG_INFO(LOG_TAG, "Initial image missing; creating: %s", path);
        result = create_blank_image_at_path(path);
        if (result != MICRO_SD_RESULT_OK) {
            return result;
        }
    } else if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Initial f_stat failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return MICRO_SD_ERROR_STAT_FAILED;
    }

    return load_card_image_from_sd(path);
}

/* Image catalog management. */

micro_sd_result_t micro_sd_create_blank_image_auto(
        char out_name[MICRO_SD_IMAGE_NAME_MAX]) {
    char name[MICRO_SD_IMAGE_NAME_MAX];
    char path[MICRO_SD_IMAGE_PATH_MAX];
    FILINFO info;
    micro_sd_result_t result;

    if (out_name == NULL) {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    out_name[0] = '\0';

    result = micro_sd_internal_mount();
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    for (unsigned i = 0; i <= 999u; ++i) {
        (void)snprintf(name, sizeof(name), "CARD%03u.MCR", i);
        micro_sd_internal_name_to_path(name, path, sizeof(path));

        FRESULT fr = f_stat(path, &info);

        if (fr == FR_NO_FILE) {
            result = create_blank_image_at_path(path);
            if (result != MICRO_SD_RESULT_OK) {
                return result;
            }

            micro_sd_internal_copy_string(out_name, MICRO_SD_IMAGE_NAME_MAX, name);
            return MICRO_SD_RESULT_OK;
        }

        if (fr != FR_OK) {
            LOG_ERROR(LOG_TAG,
                      "Image name f_stat failed: path=%s, error=%s (%d)",
                      path,
                      FRESULT_str(fr),
                      fr);
            return MICRO_SD_ERROR_STAT_FAILED;
        }
    }

    LOG_ERROR(LOG_TAG, "No free CARDxxx.MCR name found");
    return MICRO_SD_ERROR_NO_FREE_IMAGE_NAME;
}

static void sort_images(micro_sd_image_entry_t *entries, size_t count) {
    for (size_t i = 1u; i < count; ++i) {
        micro_sd_image_entry_t value = entries[i];
        size_t j = i;

        while (j > 0u && strcmp(entries[j - 1u].name, value.name) > 0) {
            entries[j] = entries[j - 1u];
            --j;
        }

        entries[j] = value;
    }
}

size_t micro_sd_list_images(micro_sd_image_entry_t *entries, size_t max_entries) {
    DIR dir;
    FILINFO info;
    FRESULT fr;
    size_t count = 0;

    if (entries == NULL ||
        max_entries == 0u ||
        micro_sd_internal_mount() != MICRO_SD_RESULT_OK) {
        return 0;
    }

    fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_opendir failed: error=%s (%d)",
                  FRESULT_str(fr),
                  fr);
        return 0;
    }

    while (count < max_entries) {
        fr = f_readdir(&dir, &info);

        if (fr != FR_OK || info.fname[0] == '\0') {
            break;
        }

        if ((info.fattrib & AM_DIR) != 0u || !has_mcr_extension(info.fname)) {
            continue;
        }

        char path[MICRO_SD_IMAGE_PATH_MAX];
        micro_sd_internal_name_to_path(info.fname, path, sizeof(path));

        if (!stat_valid_image(path, &info)) {
            continue;
        }

        micro_sd_internal_copy_string(entries[count].name,
                                      sizeof(entries[count].name),
                                      info.fname);
        ++count;
    }

    (void)f_closedir(&dir);
    sort_images(entries, count);
    return count;
}

/* PS1 save-directory parsing. */

static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void copy_save_name(const uint8_t *raw_name,
                           char *out,
                           size_t out_size,
                           uint8_t slot) {
    size_t length = 0;

    if (out == NULL || out_size == 0u) {
        return;
    }

    for (size_t i = 0; i < PS1_DIR_NAME_SIZE && length + 1u < out_size; ++i) {
        uint8_t c = raw_name[i];

        if (c == 0u || c == 0xFFu) {
            break;
        }

        if (c < 0x20u || c > 0x7Eu) {
            c = '?';
        }

        out[length++] = (char)c;
    }

    while (length > 0u && out[length - 1u] == ' ') {
        --length;
    }

    out[length] = '\0';

    if (length == 0u) {
        (void)snprintf(out, out_size, "SLOT %02u", (unsigned)slot);
    }
}

size_t micro_sd_list_saves(const char *image_name,
                           micro_sd_save_entry_t *entries,
                           size_t max_entries) {
    char path[MICRO_SD_IMAGE_PATH_MAX];
    FIL file;
    FRESULT fr;
    size_t count = 0;

    if (entries == NULL ||
        max_entries == 0u ||
        micro_sd_internal_mount() != MICRO_SD_RESULT_OK) {
        return 0;
    }

    micro_sd_internal_name_to_path(image_name, path, sizeof(path));

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_open saves failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return 0;
    }

    for (uint8_t slot = 1u;
         slot <= PS1_DIR_ENTRY_COUNT && count < max_entries;
         ++slot) {
        uint8_t frame[PS1_FRAME_SIZE];
        UINT read_bytes = 0;
        FSIZE_t offset = (FSIZE_t)slot * PS1_FRAME_SIZE;

        fr = f_lseek(&file, offset);
        if (fr == FR_OK) {
            fr = f_read(&file, frame, sizeof(frame), &read_bytes);
        }

        if (fr != FR_OK || read_bytes != sizeof(frame)) {
            LOG_ERROR(LOG_TAG,
                      "Save directory read failed: path=%s, slot=%u, "
                      "error=%s (%d), read=%u",
                      path,
                      slot,
                      FRESULT_str(fr),
                      fr,
                      read_bytes);
            break;
        }

        if (frame[0] != PS1_DIR_USED_FIRST) {
            continue;
        }

        uint32_t size_bytes = read_le32(&frame[4]);
        uint8_t blocks = (uint8_t)((size_bytes + PS1_BLOCK_SIZE - 1u) /
                                   PS1_BLOCK_SIZE);

        if (blocks == 0u) {
            blocks = 1u;
        } else if (blocks > PS1_DIR_ENTRY_COUNT) {
            blocks = PS1_DIR_ENTRY_COUNT;
        }

        entries[count].slot = slot;
        entries[count].blocks = blocks;
        entries[count].size_bytes = size_bytes;
        copy_save_name(&frame[PS1_DIR_NAME_OFFSET],
                       entries[count].file_name,
                       sizeof(entries[count].file_name),
                       slot);
        ++count;
    }

    (void)f_close(&file);
    return count;
}

/* Image activation and deletion. */

micro_sd_result_t micro_sd_activate_image_as_inserted_card(
        const char *image_name) {
    char path[MICRO_SD_IMAGE_PATH_MAX];
    micro_sd_result_t result;

    if (image_name == NULL || image_name[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    micro_sd_internal_name_to_path(image_name, path, sizeof(path));

    ps1_bus_request_pause_blocking();

    result = micro_sd_save_worker_flush();
    if (result == MICRO_SD_RESULT_OK) {
        result = load_card_image_from_sd(path);
    }

    if (result == MICRO_SD_RESULT_OK) {
        ps1emu_storage_state_init();
        micro_sd_save_worker_init(path);
        ps1_bus_begin_card_swap_absent();
    }

    ps1_bus_release_pause();
    return result;
}

static micro_sd_result_t find_delete_fallback(
        const char *deleted_name,
        char fallback[MICRO_SD_IMAGE_NAME_MAX]) {
    micro_sd_image_entry_t images[MICRO_SD_MAX_IMAGES];
    size_t count = micro_sd_list_images(images, MICRO_SD_MAX_IMAGES);

    for (size_t i = 0; i < count; ++i) {
        if (!micro_sd_internal_names_equal_ignore_case(images[i].name, deleted_name)) {
            micro_sd_internal_copy_string(fallback, MICRO_SD_IMAGE_NAME_MAX, images[i].name);
            return MICRO_SD_RESULT_OK;
        }
    }

    return micro_sd_create_blank_image_auto(fallback);
}

micro_sd_result_t micro_sd_delete_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];
    char path[MICRO_SD_IMAGE_PATH_MAX];
    bool was_active;
    FRESULT fr;
    micro_sd_result_t result;

    micro_sd_internal_copy_name_from_path(image_name, name, sizeof(name));

    if (name[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    micro_sd_internal_name_to_path(name, path, sizeof(path));
    was_active = micro_sd_is_active_image(name);

    if (was_active) {
        char fallback[MICRO_SD_IMAGE_NAME_MAX];

        result = find_delete_fallback(name, fallback);
        if (result != MICRO_SD_RESULT_OK) {
            return result;
        }

        result = micro_sd_activate_image_as_inserted_card(fallback);
        if (result != MICRO_SD_RESULT_OK) {
            return result;
        }
    } else {
        ps1_bus_request_pause_blocking();

        result = micro_sd_save_worker_flush();
        if (result != MICRO_SD_RESULT_OK) {
            ps1_bus_release_pause();
            return result;
        }
    }

    fr = f_unlink(path);

    if (!was_active) {
        ps1_bus_release_pause();
    }

    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_unlink failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return fr == FR_NO_FILE
                ? MICRO_SD_ERROR_FILE_NOT_FOUND
                : MICRO_SD_ERROR_DELETE_FAILED;
    }

    LOG_INFO(LOG_TAG, "Deleted card image: %s", path);
    return MICRO_SD_RESULT_OK;
}
