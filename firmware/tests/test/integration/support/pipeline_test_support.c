#include "unity.h"

#include "ff.h"
#include "micro_sd/micro_sd_image.h"
#include "micro_sd/micro_sd_internal.h"
#include "micro_sd/micro_sd_worker.h"
#include "pico/stdlib.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_bus_internal.h"
#include "ps1/ps1_card_emulator.h"

#include "pipeline_test_support.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_FILE_CAPACITY 4u
#define FAKE_PATH_CAPACITY 32u
#define SCRIPT_CAPACITY    160u

#define PS1_DIR_FRAME_COUNT  16u
#define PS1_DIR_CHECKSUM_POS 127u
#define PS1_DIR_FREE         0xA0u

enum {
    ACCESS_INDEX = 0,
    COMMAND_INDEX = 1,
    READ_ADDR_MSB_INDEX = 4,
    READ_ADDR_LSB_INDEX = 5,
    READ_DATA_INDEX = 10,
    READ_CHECKSUM_INDEX = READ_DATA_INDEX + PS1_FRAME_SIZE,
    READ_RESULT_INDEX = READ_CHECKSUM_INDEX + 1,
    READ_TRANSFER_COUNT = READ_RESULT_INDEX + 1,
    WRITE_ADDR_MSB_INDEX = 4,
    WRITE_ADDR_LSB_INDEX = 5,
    WRITE_DATA_INDEX = 6,
    WRITE_CHECKSUM_INDEX = WRITE_DATA_INDEX + PS1_FRAME_SIZE,
    WRITE_TRAILER_INDEX = WRITE_CHECKSUM_INDEX + 1,
    WRITE_RESULT_INDEX = WRITE_TRAILER_INDEX + 2,
    WRITE_TRANSFER_COUNT = WRITE_RESULT_INDEX + 1,
};

typedef struct {
    bool used;
    char path[FAKE_PATH_CAPACITY];
    uint8_t *data;
    uint8_t *durable_data;
    size_t size;
    size_t durable_size;
    size_t capacity;
} fake_file_t;

typedef struct {
    uint8_t rx[SCRIPT_CAPACITY];
    uint8_t tx[SCRIPT_CAPACITY];
    size_t length;
    size_t position;
} xfer_script_t;

typedef struct {
    bool fail_next_write;
    uint32_t fail_write_on_call;
    bool fail_next_sync;
    bool remove_sd_during_next_write;
    UINT partial_write_count;
    bool fail_read_once;
    char fail_read_path[FAKE_PATH_CAPACITY];
    UINT partial_read_count;
    int64_t write_delay_us;
    int64_t sync_delay_us;
} failure_control_t;

const absolute_time_t nil_time = {.us_since_boot = 0};

static fake_file_t fake_files[FAKE_FILE_CAPACITY];
static xfer_script_t script;
static failure_control_t failure;
static bool physical_card_present;
static char active_path[MICRO_SD_IMAGE_PATH_MAX];
static char active_name[MICRO_SD_IMAGE_NAME_MAX];
static micro_sd_result_t storage_result;
static int64_t now_us;
static uint32_t write_count;
static uint32_t sync_count;
static uint32_t close_count;
static uint32_t fatfs_reset_count;
static uint32_t mount_count;
static FSIZE_t last_write_offset;
static UINT last_write_size;

static const char *base_name(const char *path) {
    const char *base = path;

    for (const char *p = path; p != NULL && *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            base = p + 1;
        }
    }

    return base;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    (void)snprintf(dst, dst_size, "%s", src == NULL ? "" : src);
}

static fake_file_t *find_file(const char *path) {
    for (size_t i = 0u; i < FAKE_FILE_CAPACITY; ++i) {
        if (fake_files[i].used
                && strcmp(fake_files[i].path, path) == 0) {
            return &fake_files[i];
        }
    }

    return NULL;
}

static bool ensure_file_capacity(fake_file_t *file, size_t required) {
    if (required <= file->capacity) {
        return true;
    }

    size_t capacity = file->capacity == 0u ? 256u : file->capacity;
    while (capacity < required) {
        capacity *= 2u;
    }

    uint8_t *data = realloc(file->data, capacity);
    if (data == NULL) {
        return false;
    }

    uint8_t *durable_data = realloc(file->durable_data, capacity);
    if (durable_data == NULL) {
        file->data = data;
        return false;
    }

    memset(&data[file->capacity], 0, capacity - file->capacity);
    memset(&durable_data[file->capacity],
           0,
           capacity - file->capacity);
    file->data = data;
    file->durable_data = durable_data;
    file->capacity = capacity;
    return true;
}

static void commit_file(fake_file_t *file) {
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_TRUE(ensure_file_capacity(file, file->size));
    memcpy(file->durable_data, file->data, file->size);
    file->durable_size = file->size;
}

static void discard_unsynced_file_changes(void) {
    for (size_t i = 0u; i < FAKE_FILE_CAPACITY; ++i) {
        fake_file_t *file = &fake_files[i];

        if (!file->used) {
            continue;
        }

        TEST_ASSERT_TRUE(
                ensure_file_capacity(file, file->durable_size));
        memcpy(file->data, file->durable_data, file->durable_size);
        file->size = file->durable_size;
    }
}

static fake_file_t *add_file(const char *path, size_t size) {
    TEST_ASSERT_NULL(find_file(path));

    for (size_t i = 0u; i < FAKE_FILE_CAPACITY; ++i) {
        fake_file_t *file = &fake_files[i];

        if (file->used) {
            continue;
        }

        memset(file, 0, sizeof(*file));
        file->used = true;
        copy_string(file->path, sizeof(file->path), path);
        TEST_ASSERT_TRUE(ensure_file_capacity(file, size));
        file->size = size;
        file->durable_size = size;
        return file;
    }

    TEST_FAIL_MESSAGE("Fake filesystem is full");
    return NULL;
}

static uint8_t directory_checksum(
        const uint8_t frame[PS1_FRAME_SIZE]) {
    uint8_t checksum = 0u;

    for (size_t i = 0u; i < PS1_DIR_CHECKSUM_POS; ++i) {
        checksum ^= frame[i];
    }

    return checksum;
}

static void format_valid_image(uint8_t image[PS1_CARD_SIZE]) {
    memset(image, 0, PS1_CARD_SIZE);
    image[0] = 'M';
    image[1] = 'C';
    image[PS1_DIR_CHECKSUM_POS] = directory_checksum(image);

    for (size_t i = 1u; i < PS1_DIR_FRAME_COUNT; ++i) {
        uint8_t *entry = &image[i * PS1_FRAME_SIZE];
        entry[0] = PS1_DIR_FREE;
        entry[8] = 0xFFu;
        entry[9] = 0xFFu;
        entry[PS1_DIR_CHECKSUM_POS] = directory_checksum(entry);
    }
}

static fake_file_t *add_valid_image(const char *path) {
    fake_file_t *file = add_file(path, PS1_CARD_SIZE);
    format_valid_image(file->data);
    commit_file(file);
    return file;
}

static void reset_fake_files(void) {
    for (size_t i = 0u; i < FAKE_FILE_CAPACITY; ++i) {
        free(fake_files[i].data);
        free(fake_files[i].durable_data);
    }

    memset(fake_files, 0, sizeof(fake_files));
}

FRESULT f_stat(const char *path, FILINFO *info) {
    if (!physical_card_present) {
        return FR_NOT_READY;
    }

    fake_file_t *file = find_file(path);
    if (file == NULL) {
        return FR_NO_FILE;
    }

    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->fsize = file->size;
        copy_string(info->fname, sizeof(info->fname), base_name(path));
    }

    return FR_OK;
}

FRESULT f_open(FIL *file, const char *path, BYTE mode) {
    if (!physical_card_present) {
        return FR_NOT_READY;
    }

    fake_file_t *entry = find_file(path);

    if ((mode & FA_CREATE_NEW) != 0u) {
        if (entry != NULL) {
            return FR_EXIST;
        }

        entry = add_file(path, 0u);
    } else if (entry == NULL) {
        return FR_NO_FILE;
    }

    file->object = entry;
    file->position = 0u;
    file->mode = mode;
    return FR_OK;
}

FRESULT f_read(FIL *file, void *data, UINT count, UINT *read_count) {
    fake_file_t *entry = file->object;

    if (failure.fail_read_once
            && strcmp(entry->path, failure.fail_read_path) == 0) {
        UINT actual = failure.partial_read_count < count
                ? failure.partial_read_count
                : count;

        memcpy(data, &entry->data[(size_t)file->position], actual);
        file->position += actual;
        *read_count = actual;
        failure.fail_read_once = false;
        physical_card_present = false;
        return FR_NOT_READY;
    }

    if (!physical_card_present) {
        *read_count = 0u;
        return FR_NOT_READY;
    }

    size_t position = (size_t)file->position;
    size_t available = position < entry->size
            ? entry->size - position
            : 0u;
    UINT actual = available < count ? (UINT)available : count;

    memcpy(data, &entry->data[position], actual);
    file->position += actual;
    *read_count = actual;
    return FR_OK;
}

FSIZE_t f_size(FIL *file) {
    fake_file_t *entry = file->object;
    return entry == NULL ? 0u : entry->size;
}

FRESULT f_write(FIL *file,
                const void *data,
                UINT count,
                UINT *written) {
    ++write_count;
    now_us += failure.write_delay_us;
    last_write_offset = file->position;
    last_write_size = 0u;

    if (failure.fail_next_write
            || (failure.fail_write_on_call != 0u
                && write_count == failure.fail_write_on_call)) {
        failure.fail_next_write = false;
        failure.fail_write_on_call = 0u;
        *written = 0u;
        return FR_DISK_ERR;
    }

    if (!physical_card_present) {
        *written = 0u;
        return FR_NOT_READY;
    }

    fake_file_t *entry = file->object;
    size_t position = (size_t)file->position;
    size_t end = position + count;

    if (!ensure_file_capacity(entry, end)) {
        *written = 0u;
        return FR_DISK_ERR;
    }

    if (failure.remove_sd_during_next_write) {
        UINT actual = failure.partial_write_count < count
                ? failure.partial_write_count
                : count;

        memcpy(&entry->data[position], data, actual);
        file->position += actual;
        if ((size_t)file->position > entry->size) {
            entry->size = (size_t)file->position;
        }

        failure.remove_sd_during_next_write = false;
        physical_card_present = false;
        last_write_size = actual;
        *written = actual;
        return FR_NOT_READY;
    }

    memcpy(&entry->data[position], data, count);
    file->position = end;
    if (end > entry->size) {
        entry->size = end;
    }

    last_write_size = count;
    *written = count;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset) {
    if (!physical_card_present) {
        return FR_NOT_READY;
    }

    file->position = offset;
    return FR_OK;
}

FRESULT f_sync(FIL *file) {
    ++sync_count;
    now_us += failure.sync_delay_us;

    if (failure.fail_next_sync) {
        failure.fail_next_sync = false;
        return FR_DISK_ERR;
    }

    if (!physical_card_present) {
        return FR_NOT_READY;
    }

    commit_file(file->object);
    return FR_OK;
}

FRESULT f_close(FIL *file) {
    ++close_count;
    file->object = NULL;
    return physical_card_present ? FR_OK : FR_NOT_READY;
}

FRESULT f_unlink(const char *path) {
    fake_file_t *file = find_file(path);
    if (file == NULL) {
        return FR_NO_FILE;
    }

    free(file->data);
    free(file->durable_data);
    memset(file, 0, sizeof(*file));
    return FR_OK;
}

FRESULT f_opendir(DIR *dir, const char *path) {
    (void)path;
    dir->index = 0u;
    return physical_card_present ? FR_OK : FR_NOT_READY;
}

FRESULT f_readdir(DIR *dir, FILINFO *info) {
    while (dir->index < FAKE_FILE_CAPACITY) {
        fake_file_t *file = &fake_files[dir->index++];

        if (!file->used) {
            continue;
        }

        memset(info, 0, sizeof(*info));
        info->fsize = file->size;
        copy_string(info->fname,
                    sizeof(info->fname),
                    base_name(file->path));
        return FR_OK;
    }

    memset(info, 0, sizeof(*info));
    return FR_OK;
}

FRESULT f_closedir(DIR *dir) {
    (void)dir;
    return FR_OK;
}

const char *FRESULT_str(FRESULT result) {
    (void)result;
    return "fake FatFs result";
}

char micro_sd_internal_ascii_upper(char c) {
    return c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
}

bool micro_sd_internal_names_equal_ignore_case(const char *a,
                                               const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (micro_sd_internal_ascii_upper(*a++)
                != micro_sd_internal_ascii_upper(*b++)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

void micro_sd_internal_copy_string(char *dst,
                                   size_t dst_size,
                                   const char *src) {
    copy_string(dst, dst_size, src);
}

void micro_sd_internal_copy_name_from_path(const char *path,
                                           char *name,
                                           size_t name_size) {
    copy_string(name, name_size, path == NULL ? "" : base_name(path));
}

void micro_sd_internal_name_to_path(const char *name,
                                    char *path,
                                    size_t path_size) {
    if (name != NULL
            && (strstr(name, ":/") != NULL
                || strstr(name, ":\\") != NULL)) {
        copy_string(path, path_size, name);
    } else {
        (void)snprintf(path,
                       path_size,
                       "0:/%s",
                       name == NULL ? "" : name);
    }
}

void micro_sd_internal_set_active_image_path(const char *path) {
    copy_string(active_path, sizeof(active_path), path);
    copy_string(active_name, sizeof(active_name), base_name(active_path));
}

const char *micro_sd_active_image_path(void) {
    return active_path;
}

const char *micro_sd_active_image_name(void) {
    return active_name;
}

bool micro_sd_is_active_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];

    micro_sd_internal_copy_name_from_path(image_name,
                                          name,
                                          sizeof(name));
    return micro_sd_internal_names_equal_ignore_case(name, active_name);
}

bool micro_sd_card_present(void) {
    return physical_card_present;
}

micro_sd_result_t micro_sd_internal_mount(void) {
    ++mount_count;
    return physical_card_present
            ? MICRO_SD_RESULT_OK
            : MICRO_SD_ERROR_CARD_NOT_PRESENT;
}

micro_sd_result_t micro_sd_internal_storage_result(void) {
    return storage_result;
}

void micro_sd_internal_set_storage_result(micro_sd_result_t result) {
    storage_result = result;
}

void micro_sd_internal_reset_fatfs_card_state(void) {
    ++fatfs_reset_count;
}

void micro_sd_handle_card_unavailable(void) {
    storage_result = MICRO_SD_ERROR_CARD_NOT_PRESENT;
    ps1_bus_set_card_present(false);
    ps1emu_rollback_unconfirmed_frames();
    micro_sd_internal_worker_reset();
    micro_sd_internal_reset_fatfs_card_state();
}

void oled_show_saving(uint16_t frame_addr) {
    (void)frame_addr;
}

void oled_show_ready_for_image(const char *image_name) {
    (void)image_name;
}

void oled_show_sd_error(void) {
}

int gpio_get(uint gpio) {
    (void)gpio;
    return 1;
}

void gpio_put(uint gpio, bool value) {
    (void)gpio;
    (void)value;
}

void gpio_set_dir(uint gpio, bool out) {
    (void)gpio;
    (void)out;
}

uint32_t time_us_32(void) {
    return (uint32_t)now_us;
}

absolute_time_t get_absolute_time(void) {
    absolute_time_t result = {.us_since_boot = now_us};
    return result;
}

int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return to.us_since_boot - from.us_since_boot;
}

void tight_loop_contents(void) {
}

void busy_wait_us_32(uint32_t delay_us) {
    now_us += delay_us;
}

void busy_wait_ms(uint32_t delay_ms) {
    now_us += (int64_t)delay_ms * 1000;
}

void app_log_write(int level,
                   const char *module,
                   const char *format,
                   ...) {
    (void)level;
    (void)module;
    (void)format;
}

static ps1_bus_xfer_result_t scripted_xfer(uint8_t tx,
                                           uint8_t *rx,
                                           bool send_ack) {
    (void)send_ack;
    TEST_ASSERT_TRUE(script.position < script.length);

    size_t index = script.position++;
    script.tx[index] = tx;

    if (rx != NULL) {
        *rx = script.rx[index];
    }

    return PS1_BUS_XFER_OK;
}

static void record_ack(void) {
}

static void prepare_script(size_t length) {
    memset(&script, 0, sizeof(script));
    TEST_ASSERT_TRUE(length <= SCRIPT_CAPACITY);
    script.length = length;
}

static uint8_t frame_xor(uint16_t frame_addr,
                         const uint8_t data[PS1_FRAME_SIZE]) {
    uint8_t checksum = (uint8_t)(frame_addr & 0xFFu)
            ^ (uint8_t)(frame_addr >> 8);

    for (size_t i = 0u; i < PS1_FRAME_SIZE; ++i) {
        checksum ^= data[i];
    }

    return checksum;
}

void pipeline_prepare_write(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]) {
    prepare_script(WRITE_TRANSFER_COUNT);
    script.rx[ACCESS_INDEX] = 0x81u;
    script.rx[COMMAND_INDEX] = 0x57u;
    script.rx[WRITE_ADDR_MSB_INDEX] = (uint8_t)(frame_addr >> 8);
    script.rx[WRITE_ADDR_LSB_INDEX] = (uint8_t)frame_addr;
    memcpy(&script.rx[WRITE_DATA_INDEX], data, PS1_FRAME_SIZE);
    script.rx[WRITE_CHECKSUM_INDEX] = frame_xor(frame_addr, data);
}

void pipeline_prepare_read(uint16_t frame_addr) {
    prepare_script(READ_TRANSFER_COUNT);
    script.rx[ACCESS_INDEX] = 0x81u;
    script.rx[COMMAND_INDEX] = 0x52u;
    script.rx[READ_ADDR_MSB_INDEX] = (uint8_t)(frame_addr >> 8);
    script.rx[READ_ADDR_LSB_INDEX] = (uint8_t)frame_addr;
}

bool pipeline_run_console_transaction(void) {
    if (ps1_bus_should_ignore_transaction_for_swap()) {
        return false;
    }

    ps1emu_handle_transaction();
    return true;
}

void pipeline_console_write(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]) {
    pipeline_prepare_write(frame_addr, data);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT32(WRITE_TRANSFER_COUNT, script.position);
    TEST_ASSERT_EQUAL_HEX8(0x47u, script.tx[WRITE_RESULT_INDEX]);
}

void pipeline_fill_pattern(uint8_t data[PS1_FRAME_SIZE], uint8_t seed) {
    for (size_t i = 0u; i < PS1_FRAME_SIZE; ++i) {
        data[i] = (uint8_t)(seed + (uint8_t)(i * 3u));
    }
}

void pipeline_initialize_image_a(void) {
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_load_or_create_initial_image(PIPELINE_IMAGE_A_PATH));
    ps1emu_storage_state_init();
    micro_sd_save_worker_init(PIPELINE_IMAGE_A_PATH);
    ps1_bus_set_card_present(true);
}

void pipeline_restart_firmware(const char *path) {
    char restart_path[MICRO_SD_IMAGE_PATH_MAX];

    copy_string(restart_path, sizeof(restart_path), path);
    discard_unsynced_file_changes();
    memset(&failure, 0, sizeof(failure));
    memset(&script, 0, sizeof(script));
    memset(card_image, 0, sizeof(card_image));
    storage_result = MICRO_SD_RESULT_OK;
    micro_sd_internal_worker_reset();
    ps1emu_storage_state_init();
    ps1_bus_test_reset_state();
    ps1_bus_test_set_pause_auto_ack(true);
    ps1_bus_test_set_transport(scripted_xfer, record_ack);

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_load_or_create_initial_image(restart_path));
    ps1emu_storage_state_init();
    micro_sd_save_worker_init(restart_path);
    ps1_bus_set_card_present(true);
}

void pipeline_reconnect_storage(void) {
    char path[MICRO_SD_IMAGE_PATH_MAX];

    copy_string(path, sizeof(path), active_path);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_internal_mount());
    micro_sd_save_worker_init(path);
    ps1_bus_set_card_present(true);
}

void pipeline_assert_frame_is_dirty(
        uint16_t expected_addr,
        const uint8_t expected_data[PS1_FRAME_SIZE]) {
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;
    uint8_t data[PS1_FRAME_SIZE];

    ps1emu_rollback_unconfirmed_frames();
    TEST_ASSERT_EQUAL_INT(
            PS1EMU_RESULT_OK,
            ps1emu_take_changed_frame(&frame_addr,
                                      &frame_version,
                                      data));
    TEST_ASSERT_EQUAL_UINT16(expected_addr, frame_addr);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_data, data, PS1_FRAME_SIZE);
    ps1emu_rollback_unconfirmed_frames();
}

void pipeline_assert_no_dirty_frames(void) {
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;
    uint8_t data[PS1_FRAME_SIZE];

    ps1emu_rollback_unconfirmed_frames();
    TEST_ASSERT_EQUAL_INT(
            PS1EMU_RESULT_NO_CHANGED_FRAME,
            ps1emu_take_changed_frame(&frame_addr,
                                      &frame_version,
                                      data));
}

void pipeline_finish_card_swap_delay(void) {
    TEST_ASSERT_TRUE(ps1_bus_should_ignore_transaction_for_swap());
    now_us += 1500 * 1000;
    TEST_ASSERT_TRUE(ps1_bus_should_ignore_transaction_for_swap());
    TEST_ASSERT_FALSE(ps1_bus_should_ignore_transaction_for_swap());
}

uint8_t *pipeline_add_valid_image(const char *path) {
    return add_valid_image(path)->data;
}

const uint8_t *pipeline_durable_image_data(const char *path) {
    fake_file_t *file = find_file(path);
    TEST_ASSERT_NOT_NULL(file);
    return file->durable_data;
}

const uint8_t *pipeline_read_response_data(void) {
    return &script.tx[READ_DATA_INDEX];
}

size_t pipeline_script_position(void) {
    return script.position;
}

void pipeline_fail_next_write(void) {
    failure.fail_next_write = true;
}

void pipeline_fail_write_on_call(uint32_t call_number) {
    failure.fail_write_on_call = call_number;
}

void pipeline_fail_next_sync(void) {
    failure.fail_next_sync = true;
}

void pipeline_remove_sd_during_next_write(UINT partial_write_count) {
    failure.remove_sd_during_next_write = true;
    failure.partial_write_count = partial_write_count;
}

void pipeline_fail_next_read_partially(const char *path,
                                       UINT partial_read_count) {
    failure.fail_read_once = true;
    failure.partial_read_count = partial_read_count;
    copy_string(failure.fail_read_path,
                sizeof(failure.fail_read_path),
                path);
}

void pipeline_set_storage_delays(int64_t write_delay_us,
                                 int64_t sync_delay_us) {
    failure.write_delay_us = write_delay_us;
    failure.sync_delay_us = sync_delay_us;
}

void pipeline_set_physical_card_present(bool present) {
    physical_card_present = present;
}

bool pipeline_physical_card_present(void) {
    return physical_card_present;
}

void pipeline_advance_time_us(int64_t delta_us) {
    now_us += delta_us;
}

uint32_t pipeline_write_count(void) {
    return write_count;
}

uint32_t pipeline_sync_count(void) {
    return sync_count;
}

uint32_t pipeline_fatfs_reset_count(void) {
    return fatfs_reset_count;
}

uint32_t pipeline_mount_count(void) {
    return mount_count;
}

FSIZE_t pipeline_last_write_offset(void) {
    return last_write_offset;
}

UINT pipeline_last_write_size(void) {
    return last_write_size;
}

void pipeline_test_set_up(void) {
    reset_fake_files();
    memset(&failure, 0, sizeof(failure));
    memset(&script, 0, sizeof(script));
    memset(card_image, 0, sizeof(card_image));
    physical_card_present = true;
    storage_result = MICRO_SD_RESULT_OK;
    now_us = 0;
    write_count = 0u;
    sync_count = 0u;
    close_count = 0u;
    fatfs_reset_count = 0u;
    mount_count = 0u;
    last_write_offset = 0u;
    last_write_size = 0u;
    micro_sd_internal_set_active_image_path(PIPELINE_IMAGE_A_PATH);
    micro_sd_internal_worker_reset();
    ps1emu_storage_state_init();
    ps1_bus_test_reset_state();
    ps1_bus_test_set_pause_auto_ack(true);
    ps1_bus_test_set_transport(scripted_xfer, record_ack);
}

void pipeline_test_tear_down(void) {
    reset_fake_files();
}
