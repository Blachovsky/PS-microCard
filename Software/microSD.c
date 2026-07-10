#include "microSD.h"

#include "oled.h"
#include "ps1_card_bus.h"
#include "ps1_card_emulator.h"

#include "diskio.h"
#include "ff.h"
#include "f_util.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SD_SYNC_IDLE_DELAY_MS 250u
#define SD_RETRY_DELAY_MS     1000u

#define PS1_BLOCK_SIZE        (PS1_FRAME_SIZE * 64u)
#define PS1_DIR_FRAME_COUNT   16u
#define PS1_DIR_ENTRY_COUNT   15u
#define PS1_DIR_NAME_OFFSET   10u
#define PS1_DIR_NAME_SIZE     20u
#define PS1_DIR_CHECKSUM_POS  127u

#define PS1_DIR_FREE          0xA0u
#define PS1_DIR_USED_FIRST    0x51u

typedef struct {
    bool file_open;
    bool needs_sync;
    absolute_time_t last_write_time;
} save_worker_state_t;

static FATFS fs;
static FIL save_file;
static uint8_t save_frame_data[PS1_FRAME_SIZE];
static bool unsynced_frames[PS1_FRAME_COUNT];
static uint32_t unsynced_versions[PS1_FRAME_COUNT];
static bool fs_mounted;
static bool card_detect_initialized;
static bool storage_ready;
static volatile bool card_removed_latched;

static char active_image_path[MICRO_SD_IMAGE_PATH_MAX] = "0:/CARD000.MCR";
static char active_image_name[MICRO_SD_IMAGE_NAME_MAX] = "CARD000.MCR";
static save_worker_state_t save_worker;

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

bool micro_sd_storage_ready(void) {
    return storage_ready && micro_sd_card_present();
}

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    return c;
}

static bool names_equal_ignore_case(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (ascii_upper(*a++) != ascii_upper(*b++)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        src = "";
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

static void copy_name_from_path(const char *path, char *name, size_t name_size) {
    const char *base = path;

    if (path == NULL) {
        copy_string(name, name_size, "");
        return;
    }

    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            base = p + 1;
        }
    }

    copy_string(name, name_size, base);
}

static void micro_sd_name_to_path(const char *name,
                                  char *path,
                                  size_t path_size) {
    if (path == NULL || path_size == 0u) {
        return;
    }

    if (name == NULL) {
        name = "";
    }

    if (strstr(name, ":/") != NULL || strstr(name, ":\\") != NULL) {
        copy_string(path, path_size, name);
    } else {
        (void)snprintf(path, path_size, "0:/%s", name);
    }
}

static void set_active_image_path(const char *path) {
    copy_string(active_image_path, sizeof(active_image_path), path);
    copy_name_from_path(active_image_path,
                        active_image_name,
                        sizeof(active_image_name));
}

const char *micro_sd_active_image_path(void) {
    return active_image_path;
}

const char *micro_sd_active_image_name(void) {
    return active_image_name;
}

bool micro_sd_is_active_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];

    copy_name_from_path(image_name, name, sizeof(name));
    return names_equal_ignore_case(name, active_image_name);
}

static bool micro_sd_mount(void) {
    if (!micro_sd_card_present()) {
        fs_mounted = false;
        (void)f_mount(NULL, "0:", 0);
        return false;
    }

    if (fs_mounted) {
        return true;
    }

    FRESULT fr = f_mount(&fs, "0:", 1);

    if (fr != FR_OK) {
        printf("f_mount failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    fs_mounted = true;
    return true;
}

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
           ascii_upper(ext[1]) == 'M' &&
           ascii_upper(ext[2]) == 'C' &&
           ascii_upper(ext[3]) == 'R';
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

bool micro_sd_active_image_accessible(void) {
    FIL file;
    FRESULT fr;
    UINT read_bytes = 0;
    uint8_t probe_byte = 0;
    bool accessible = false;

    if (save_worker.file_open || save_worker.needs_sync) {
        return true;
    }

    if (!micro_sd_mount()) {
        return false;
    }

    fr = f_open(&file, active_image_path, FA_READ);
    if (fr != FR_OK) {
        return false;
    }

    if (f_size(&file) == PS1_CARD_SIZE) {
        fr = f_read(&file, &probe_byte, sizeof(probe_byte), &read_bytes);
        accessible = fr == FR_OK && read_bytes == sizeof(probe_byte);
    }

    fr = f_close(&file);
    return accessible && fr == FR_OK;
}

static bool card_image_is_erased_blank(void);
static bool overwrite_existing_image_with_blank_format(const char *path);

static bool load_card_image_from_sd(const char *path) {
    FIL file;
    FILINFO info;
    FRESULT fr;
    UINT read_bytes;

    if (!micro_sd_mount()) {
        return false;
    }

    fr = f_stat(path, &info);
    if (fr != FR_OK) {
        printf("f_stat failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    if (info.fsize != PS1_CARD_SIZE) {
        printf("Invalid card image size: %lu\n", (unsigned long)info.fsize);
        return false;
    }

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("f_open failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    fr = f_read(&file, card_image, PS1_CARD_SIZE, &read_bytes);
    (void)f_close(&file);

    if (fr != FR_OK || read_bytes != PS1_CARD_SIZE) {
        printf("f_read failed: fr=%d read=%u\n", fr, read_bytes);
        return false;
    }

    if (card_image_is_erased_blank()) {
        printf("Card image is erased blank, formatting: %s\n", path);

        if (!overwrite_existing_image_with_blank_format(path)) {
            return false;
        }
    }

    set_active_image_path(path);
    printf("Card image loaded: %s\n", path);
    return true;
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

static bool overwrite_existing_image_with_blank_format(const char *path) {
    FIL file;
    uint8_t frame[PS1_FRAME_SIZE];
    FRESULT fr;

    fr = f_open(&file, path, FA_WRITE | FA_OPEN_EXISTING);
    if (fr != FR_OK) {
        printf("f_open format failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        UINT written = 0;

        make_blank_card_frame(i, frame);
        fr = f_lseek(&file, (FSIZE_t)i * PS1_FRAME_SIZE);

        if (fr == FR_OK) {
            fr = f_write(&file, frame, sizeof(frame), &written);
        }

        if (fr != FR_OK || written != sizeof(frame)) {
            printf("blank format write failed: fr=%d written=%u\n", fr, written);
            (void)f_close(&file);
            return false;
        }
    }

    fr = f_sync(&file);

    if (fr == FR_OK) {
        fr = f_close(&file);
    } else {
        (void)f_close(&file);
    }

    if (fr != FR_OK) {
        printf("blank format sync/close failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    make_blank_card_image(card_image);
    printf("Blank card image formatted: %s\n", path);
    return true;
}

static bool create_blank_image_at_path(const char *path) {
    FIL file;
    uint8_t frame[PS1_FRAME_SIZE];
    FRESULT fr;

    if (!micro_sd_mount()) {
        return false;
    }

    fr = f_open(&file, path, FA_WRITE | FA_CREATE_NEW);
    if (fr != FR_OK) {
        printf("f_open create failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        UINT written = 0;

        make_blank_card_frame(i, frame);
        fr = f_write(&file, frame, sizeof(frame), &written);

        if (fr != FR_OK || written != sizeof(frame)) {
            printf("blank image write failed: fr=%d written=%u\n", fr, written);
            (void)f_close(&file);
            (void)f_unlink(path);
            return false;
        }
    }

    fr = f_sync(&file);

    if (fr == FR_OK) {
        fr = f_close(&file);
    } else {
        (void)f_close(&file);
    }

    if (fr != FR_OK) {
        printf("blank image sync/close failed: %s %d\n", FRESULT_str(fr), fr);
        (void)f_unlink(path);
        return false;
    }

    printf("Blank card image created: %s\n", path);
    return true;
}

bool micro_sd_load_or_create_initial_image(const char *path) {
    FILINFO info;
    FRESULT fr;

    if (!micro_sd_mount()) {
        return false;
    }

    fr = f_stat(path, &info);

    if (fr == FR_NO_FILE) {
        printf("Initial image missing, creating: %s\n", path);
        if (!create_blank_image_at_path(path)) {
            return false;
        }
    } else if (fr != FR_OK) {
        printf("initial f_stat failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    return load_card_image_from_sd(path);
}

static void clear_unsynced_state(void) {
    memset(unsynced_frames, 0, sizeof(unsynced_frames));
    memset(unsynced_versions, 0, sizeof(unsynced_versions));
}

static void reset_fatfs_card_state(void) {
    storage_ready = false;
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

void micro_sd_handle_card_unavailable(void) {
    ps1_bus_set_card_present(false);
    ps1emu_rollback_unconfirmed_frames();
    clear_unsynced_state();
    memset(&save_worker, 0, sizeof(save_worker));
    save_worker.last_write_time = nil_time;
    reset_fatfs_card_state();
}

static void confirm_unsynced_frames(void) {
    for (uint16_t i = 0; i < PS1_FRAME_COUNT; ++i) {
        if (unsynced_frames[i]) {
            ps1emu_confirm_frame_synced(i, unsynced_versions[i]);
        }
    }

    clear_unsynced_state();
}

static bool open_image_for_update(FIL *file, const char *path) {
    FRESULT fr = f_open(file, path, FA_WRITE | FA_OPEN_EXISTING);

    if (fr != FR_OK) {
        printf("f_open update failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    return true;
}

static void close_save_file_if_open(void) {
    if (save_worker.file_open) {
        (void)f_close(&save_file);
        save_worker.file_open = false;
    }
}

static void storage_error_recovery(void) {
    storage_ready = false;
    ps1_bus_set_card_present(false);
    ps1emu_rollback_unconfirmed_frames();
    clear_unsynced_state();
    save_worker.needs_sync = false;
    close_save_file_if_open();
    reset_fatfs_card_state();

    oled_show_sd_error();

    /* busy_wait does not use the default alarm pool running on core 0. */
    busy_wait_ms(SD_RETRY_DELAY_MS);
}

void micro_sd_save_worker_init(const char *path) {
    set_active_image_path(path);
    memset(&save_worker, 0, sizeof(save_worker));
    save_worker.last_write_time = nil_time;
    clear_unsynced_state();
    storage_ready = micro_sd_card_present();
}

static bool write_next_changed_frame(bool *did_write) {
    uint16_t frame_addr;
    uint32_t frame_version;

    if (did_write != NULL) {
        *did_write = false;
    }

    if (!ps1emu_take_changed_frame(&frame_addr,
                                   &frame_version,
                                   save_frame_data)) {
        return true;
    }

    if (!save_worker.file_open) {
        if (!open_image_for_update(&save_file, active_image_path)) {
            storage_error_recovery();
            return false;
        }

        save_worker.file_open = true;
    }

    FSIZE_t offset = (FSIZE_t)frame_addr * PS1_FRAME_SIZE;
    UINT written = 0;
    FRESULT fr = f_lseek(&save_file, offset);

    if (fr == FR_OK) {
        fr = f_write(&save_file, save_frame_data, PS1_FRAME_SIZE, &written);
    }

    if (fr != FR_OK || written != PS1_FRAME_SIZE) {
        printf("Frame write failed: frame=%u fr=%d written=%u\n",
               frame_addr, fr, written);
        storage_error_recovery();
        return false;
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

    return true;
}

static bool sync_pending_frames(void) {
    FRESULT fr = FR_OK;

    if (!save_worker.needs_sync) {
        close_save_file_if_open();
        return true;
    }

    if (save_worker.file_open) {
        fr = f_sync(&save_file);

        if (fr == FR_OK) {
            fr = f_close(&save_file);
            save_worker.file_open = false;
        }
    }

    if (fr != FR_OK) {
        printf("SD sync/close failed: %s %d\n", FRESULT_str(fr), fr);
        storage_error_recovery();
        return false;
    }

    confirm_unsynced_frames();
    save_worker.needs_sync = false;
    oled_show_ready_for_image(active_image_name);
    return true;
}

void micro_sd_save_worker_poll(void) {
    bool did_write = false;

    if (!micro_sd_card_present()) {
        micro_sd_handle_card_unavailable();
        return;
    }

    if (!write_next_changed_frame(&did_write)) {
        return;
    }

    if (did_write || !save_worker.needs_sync) {
        return;
    }

    int64_t idle_us = absolute_time_diff_us(save_worker.last_write_time,
                                            get_absolute_time());

    if (idle_us < (int64_t)SD_SYNC_IDLE_DELAY_MS * 1000) {
        return;
    }

    (void)sync_pending_frames();
}

bool micro_sd_save_worker_flush(void) {
    while (true) {
        bool did_write = false;

        if (!micro_sd_card_present()) {
            micro_sd_handle_card_unavailable();
            return false;
        }

        if (!write_next_changed_frame(&did_write)) {
            return false;
        }

        if (did_write) {
            continue;
        }

        return sync_pending_frames();
    }
}

bool micro_sd_create_blank_image_auto(char out_name[MICRO_SD_IMAGE_NAME_MAX]) {
    char name[MICRO_SD_IMAGE_NAME_MAX];
    char path[MICRO_SD_IMAGE_PATH_MAX];
    FILINFO info;

    if (out_name != NULL) {
        out_name[0] = '\0';
    }

    if (!micro_sd_mount()) {
        return false;
    }

    for (unsigned i = 0; i <= 999u; ++i) {
        (void)snprintf(name, sizeof(name), "CARD%03u.MCR", i);
        micro_sd_name_to_path(name, path, sizeof(path));

        if (f_stat(path, &info) == FR_NO_FILE) {
            if (!create_blank_image_at_path(path)) {
                return false;
            }

            copy_string(out_name, MICRO_SD_IMAGE_NAME_MAX, name);
            return true;
        }
    }

    printf("No free CARDxxx.MCR name found\n");
    return false;
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

    if (entries == NULL || max_entries == 0u || !micro_sd_mount()) {
        return 0;
    }

    fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK) {
        printf("f_opendir failed: %s %d\n", FRESULT_str(fr), fr);
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
        micro_sd_name_to_path(info.fname, path, sizeof(path));

        if (!stat_valid_image(path, &info)) {
            continue;
        }

        copy_string(entries[count].name, sizeof(entries[count].name), info.fname);
        ++count;
    }

    (void)f_closedir(&dir);
    sort_images(entries, count);
    return count;
}

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

    if (entries == NULL || max_entries == 0u || !micro_sd_mount()) {
        return 0;
    }

    micro_sd_name_to_path(image_name, path, sizeof(path));

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("f_open saves failed: %s %d\n", FRESULT_str(fr), fr);
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
            printf("read save directory failed: slot=%u fr=%d read=%u\n",
                   slot, fr, read_bytes);
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

bool micro_sd_activate_image_as_inserted_card(const char *image_name) {
    char path[MICRO_SD_IMAGE_PATH_MAX];
    bool ok = false;

    micro_sd_name_to_path(image_name, path, sizeof(path));

    ps1_bus_request_pause_blocking();

    if (micro_sd_save_worker_flush() && load_card_image_from_sd(path)) {
        ps1emu_storage_state_init();
        micro_sd_save_worker_init(path);
        ps1_bus_begin_card_swap_absent();
        ok = true;
    }

    ps1_bus_release_pause();
    return ok;
}

static bool find_delete_fallback(const char *deleted_name,
                                 char fallback[MICRO_SD_IMAGE_NAME_MAX]) {
    micro_sd_image_entry_t images[MICRO_SD_MAX_IMAGES];
    size_t count = micro_sd_list_images(images, MICRO_SD_MAX_IMAGES);

    for (size_t i = 0; i < count; ++i) {
        if (!names_equal_ignore_case(images[i].name, deleted_name)) {
            copy_string(fallback, MICRO_SD_IMAGE_NAME_MAX, images[i].name);
            return true;
        }
    }

    return micro_sd_create_blank_image_auto(fallback);
}

bool micro_sd_delete_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];
    char path[MICRO_SD_IMAGE_PATH_MAX];
    bool was_active;
    FRESULT fr;

    copy_name_from_path(image_name, name, sizeof(name));

    if (name[0] == '\0') {
        return false;
    }

    micro_sd_name_to_path(name, path, sizeof(path));
    was_active = micro_sd_is_active_image(name);

    if (was_active) {
        char fallback[MICRO_SD_IMAGE_NAME_MAX];

        if (!find_delete_fallback(name, fallback)) {
            return false;
        }

        if (!micro_sd_activate_image_as_inserted_card(fallback)) {
            return false;
        }
    } else {
        ps1_bus_request_pause_blocking();

        if (!micro_sd_save_worker_flush()) {
            ps1_bus_release_pause();
            return false;
        }
    }

    fr = f_unlink(path);

    if (!was_active) {
        ps1_bus_release_pause();
    }

    if (fr != FR_OK) {
        printf("f_unlink failed: %s %d\n", FRESULT_str(fr), fr);
        return false;
    }

    printf("Deleted card image: %s\n", path);
    return true;
}
