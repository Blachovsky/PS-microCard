#include "micro_sd/micro_sd_worker.h"

#include "micro_sd/micro_sd_internal.h"

#include "logger/app_log.h"
#include "drivers/oled.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"

#include "ff.h"
#include "f_util.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SD_SYNC_IDLE_DELAY_MS 250u
#define SD_RETRY_DELAY_MS     1000u
#define LOG_TAG               "micro_sd"

typedef struct {
    bool file_open;
    bool needs_sync;
    absolute_time_t last_write_time;
} save_worker_state_t;

static FIL save_file;
static uint8_t save_frame_data[PS1_FRAME_SIZE];
static bool unsynced_frames[PS1_FRAME_COUNT];
static uint32_t unsynced_versions[PS1_FRAME_COUNT];
static save_worker_state_t save_worker;

/* Active-image health check. */

micro_sd_result_t micro_sd_check_active_image_accessible(void) {
    FIL file;
    FRESULT fr;
    UINT read_bytes = 0;
    uint8_t probe_byte = 0;
    micro_sd_result_t result = MICRO_SD_RESULT_OK;

    if (save_worker.file_open || save_worker.needs_sync) {
        return MICRO_SD_RESULT_OK;
    }

    result = micro_sd_internal_mount();
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    fr = f_open(&file, micro_sd_active_image_path(), FA_READ);
    if (fr != FR_OK) {
        return fr == FR_NO_FILE
                ? MICRO_SD_ERROR_FILE_NOT_FOUND
                : MICRO_SD_ERROR_OPEN_FAILED;
    }

    if (f_size(&file) != PS1_CARD_SIZE) {
        result = MICRO_SD_ERROR_INVALID_IMAGE_SIZE;
    } else {
        fr = f_read(&file, &probe_byte, sizeof(probe_byte), &read_bytes);
        if (fr != FR_OK || read_bytes != sizeof(probe_byte)) {
            result = MICRO_SD_ERROR_READ_FAILED;
        }
    }

    fr = f_close(&file);
    if (result == MICRO_SD_RESULT_OK && fr != FR_OK) {
        result = MICRO_SD_ERROR_CLOSE_FAILED;
    }

    return result;
}

/* Save-worker state and recovery. */

static void clear_unsynced_state(void) {
    memset(unsynced_frames, 0, sizeof(unsynced_frames));
    memset(unsynced_versions, 0, sizeof(unsynced_versions));
}

void micro_sd_internal_worker_reset(void) {
    clear_unsynced_state();
    memset(&save_worker, 0, sizeof(save_worker));
    save_worker.last_write_time = nil_time;
}

static void confirm_unsynced_frames(void) {
    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        if (unsynced_frames[i]) {
            ps1emu_confirm_frame_synced(i, unsynced_versions[i]);
        }
    }

    clear_unsynced_state();
}

static micro_sd_result_t open_image_for_update(FIL *file, const char *path) {
    if (file == NULL || path == NULL || path[0] == '\0') {
        return MICRO_SD_ERROR_INVALID_ARGUMENT;
    }

    FRESULT fr = f_open(file, path, FA_WRITE | FA_OPEN_EXISTING);

    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "f_open for update failed: path=%s, error=%s (%d)",
                  path,
                  FRESULT_str(fr),
                  fr);
        return fr == FR_NO_FILE
                ? MICRO_SD_ERROR_FILE_NOT_FOUND
                : MICRO_SD_ERROR_OPEN_FAILED;
    }

    return MICRO_SD_RESULT_OK;
}

static void close_save_file_if_open(void) {
    if (save_worker.file_open) {
        (void)f_close(&save_file);
        save_worker.file_open = false;
    }
}

static void storage_error_recovery(micro_sd_result_t result) {
    micro_sd_internal_set_storage_result(result);
    ps1_bus_set_card_present(false);
    ps1emu_rollback_unconfirmed_frames();
    clear_unsynced_state();
    save_worker.needs_sync = false;
    close_save_file_if_open();
    micro_sd_internal_reset_fatfs_card_state();

    oled_show_sd_error();

    /* busy_wait does not use the default alarm pool running on core 0. */
    busy_wait_ms(SD_RETRY_DELAY_MS);
}

void micro_sd_save_worker_init(const char *path) {
    micro_sd_internal_set_active_image_path(path);
    memset(&save_worker, 0, sizeof(save_worker));
    save_worker.last_write_time = nil_time;
    clear_unsynced_state();
    micro_sd_internal_set_storage_result(
            micro_sd_card_present()
                    ? MICRO_SD_RESULT_OK
                    : MICRO_SD_ERROR_CARD_NOT_PRESENT);
}

/* Incremental frame write and synchronization. */

static micro_sd_result_t write_next_changed_frame(bool *did_write) {
    uint16_t frame_addr;
    uint32_t frame_version;
    micro_sd_result_t result;
    ps1emu_result_t frame_result;

    if (did_write != NULL) {
        *did_write = false;
    }

    frame_result = ps1emu_take_changed_frame(&frame_addr,
                                             &frame_version,
                                             save_frame_data);
    if (frame_result == PS1EMU_RESULT_NO_CHANGED_FRAME) {
        return MICRO_SD_RESULT_OK;
    }

    if (frame_result != PS1EMU_RESULT_OK) {
        LOG_ERROR(LOG_TAG,
                  "Frame fetch failed: result=%d",
                  (int)frame_result);
        storage_error_recovery(MICRO_SD_ERROR_FRAME_FETCH_FAILED);
        return MICRO_SD_ERROR_FRAME_FETCH_FAILED;
    }

    if (!save_worker.file_open) {
        result = open_image_for_update(&save_file, micro_sd_active_image_path());
        if (result != MICRO_SD_RESULT_OK) {
            storage_error_recovery(result);
            return result;
        }

        save_worker.file_open = true;
    }

    FSIZE_t offset = (FSIZE_t)frame_addr * PS1_FRAME_SIZE;
    UINT written = 0;
    FRESULT fr = f_lseek(&save_file, offset);

    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG,
                  "Frame seek failed: frame=%u, error=%s (%d)",
                  frame_addr,
                  FRESULT_str(fr),
                  fr);
        storage_error_recovery(MICRO_SD_ERROR_SEEK_FAILED);
        return MICRO_SD_ERROR_SEEK_FAILED;
    }

    fr = f_write(&save_file, save_frame_data, PS1_FRAME_SIZE, &written);
    if (fr != FR_OK || written != PS1_FRAME_SIZE) {
        LOG_ERROR(LOG_TAG,
                  "Frame write failed: frame=%u, error=%s (%d), written=%u",
                  frame_addr,
                  FRESULT_str(fr),
                  fr,
                  written);
        storage_error_recovery(MICRO_SD_ERROR_WRITE_FAILED);
        return MICRO_SD_ERROR_WRITE_FAILED;
    }

    unsynced_frames[frame_addr] = true;
    unsynced_versions[frame_addr] = frame_version;

    if (!save_worker.needs_sync) {
        oled_show_saving(frame_addr);
    }

    save_worker.needs_sync = true;
    save_worker.last_write_time = get_absolute_time();

    if (did_write != NULL) {
        *did_write = true;
    }

    return MICRO_SD_RESULT_OK;
}

static micro_sd_result_t sync_pending_frames(void) {
    FRESULT fr = FR_OK;

    if (!save_worker.needs_sync) {
        if (save_worker.file_open) {
            fr = f_close(&save_file);
            save_worker.file_open = false;
            if (fr != FR_OK) {
                storage_error_recovery(MICRO_SD_ERROR_CLOSE_FAILED);
                return MICRO_SD_ERROR_CLOSE_FAILED;
            }
        }

        return MICRO_SD_RESULT_OK;
    }

    if (save_worker.file_open) {
        fr = f_sync(&save_file);
        if (fr != FR_OK) {
            LOG_ERROR(LOG_TAG,
                      "SD sync failed: error=%s (%d)",
                      FRESULT_str(fr),
                      fr);
            storage_error_recovery(MICRO_SD_ERROR_SYNC_FAILED);
            return MICRO_SD_ERROR_SYNC_FAILED;
        }

        fr = f_close(&save_file);
        save_worker.file_open = false;
        if (fr != FR_OK) {
            LOG_ERROR(LOG_TAG,
                      "SD close failed: error=%s (%d)",
                      FRESULT_str(fr),
                      fr);
            storage_error_recovery(MICRO_SD_ERROR_CLOSE_FAILED);
            return MICRO_SD_ERROR_CLOSE_FAILED;
        }
    }

    confirm_unsynced_frames();
    save_worker.needs_sync = false;
    oled_show_ready_for_image(micro_sd_active_image_name());
    return MICRO_SD_RESULT_OK;
}

micro_sd_result_t micro_sd_save_worker_poll(void) {
    bool did_write = false;
    micro_sd_result_t result;

    if (!micro_sd_card_present()) {
        micro_sd_handle_card_unavailable();
        return MICRO_SD_ERROR_CARD_NOT_PRESENT;
    }

    if (micro_sd_internal_storage_result() != MICRO_SD_RESULT_OK) {
        return micro_sd_internal_storage_result();
    }

    result = write_next_changed_frame(&did_write);
    if (result != MICRO_SD_RESULT_OK) {
        return result;
    }

    if (did_write || !save_worker.needs_sync) {
        return MICRO_SD_RESULT_OK;
    }

    int64_t idle_us = absolute_time_diff_us(save_worker.last_write_time,
                                            get_absolute_time());

    if (idle_us < (int64_t)SD_SYNC_IDLE_DELAY_MS * 1000) {
        return MICRO_SD_RESULT_OK;
    }

    return sync_pending_frames();
}

micro_sd_result_t micro_sd_save_worker_flush(void) {
    while (true) {
        bool did_write = false;
        micro_sd_result_t result;

        if (!micro_sd_card_present()) {
            micro_sd_handle_card_unavailable();
            return MICRO_SD_ERROR_CARD_NOT_PRESENT;
        }

        if (micro_sd_internal_storage_result() != MICRO_SD_RESULT_OK) {
            return micro_sd_internal_storage_result();
        }

        result = write_next_changed_frame(&did_write);
        if (result != MICRO_SD_RESULT_OK) {
            return result;
        }

        if (did_write) {
            continue;
        }

        return sync_pending_frames();
    }
}
