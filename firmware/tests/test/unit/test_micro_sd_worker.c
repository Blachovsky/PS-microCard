#include "unity.h"

#include "ff.h"
#include "micro_sd/micro_sd_worker.h"
#include "pico/stdlib.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FRAME_QUEUE_CAPACITY 8u
#define EVENT_CAPACITY       64u

enum {
    TEST_PS1EMU_RESULT_OK = 0,
    TEST_PS1EMU_RESULT_NO_CHANGED_FRAME = 1,
};

typedef enum {
    EVENT_OPEN = 1,
    EVENT_SEEK,
    EVENT_WRITE,
    EVENT_SYNC,
    EVENT_CLOSE,
    EVENT_CONFIRM,
} test_event_t;

typedef struct {
    uint16_t addr;
    uint32_t version;
    uint8_t data[PS1_FRAME_SIZE];
} queued_frame_t;

typedef struct {
    FRESULT open_result;
    FRESULT seek_result;
    FRESULT write_result;
    FRESULT sync_result;
    FRESULT close_result;
    UINT write_count;
    uint32_t write_error_on_call;
    bool remove_card_when_frame_taken;
    bool remove_card_after_seek;
    bool remove_card_after_write;
    bool remove_card_during_close;
} failure_control_t;

typedef struct {
    FSIZE_t offset;
    UINT count;
    uint8_t data[PS1_FRAME_SIZE];
} write_record_t;

const absolute_time_t nil_time = {.us_since_boot = 0};

static queued_frame_t frame_queue[FRAME_QUEUE_CAPACITY];
static bool frame_confirmed[FRAME_QUEUE_CAPACITY];
static size_t frame_queue_count;
static size_t frame_queue_position;
static failure_control_t failure;
static test_event_t events[EVENT_CAPACITY];
static size_t event_count;
static write_record_t write_records[FRAME_QUEUE_CAPACITY];
static size_t write_record_count;
static uint8_t disk_image[PS1_CARD_SIZE];
static FSIZE_t file_position;
static bool card_present;
static micro_sd_result_t storage_result;
static micro_sd_result_t mount_result;
static char active_path[MICRO_SD_IMAGE_PATH_MAX];
static char active_name[MICRO_SD_IMAGE_NAME_MAX];
static int64_t now_us;
static uint32_t open_count;
static uint32_t seek_count;
static uint32_t write_count;
static uint32_t sync_count;
static uint32_t close_count;
static uint32_t confirm_count;
static uint32_t rollback_count;
static uint32_t unavailable_count;
static uint32_t fatfs_reset_count;
static uint32_t bus_card_absent_count;
static uint32_t oled_saving_count;
static uint32_t oled_ready_count;
static uint32_t oled_error_count;
static uint32_t busy_wait_total_ms;
static uint16_t confirmed_addr[FRAME_QUEUE_CAPACITY];
static uint32_t confirmed_version[FRAME_QUEUE_CAPACITY];

static void record_event(test_event_t event) {
    TEST_ASSERT_TRUE(event_count < EVENT_CAPACITY);
    events[event_count++] = event;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
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

    memmove(dst, src, copy_length);
    dst[copy_length] = '\0';
}

static const char *path_base_name(const char *path) {
    const char *base = path;

    for (const char *p = path; p != NULL && *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            base = p + 1;
        }
    }

    return base;
}

static void queue_frame(uint16_t addr, uint32_t version, uint8_t seed) {
    TEST_ASSERT_TRUE(frame_queue_count < FRAME_QUEUE_CAPACITY);

    queued_frame_t *frame = &frame_queue[frame_queue_count++];
    frame->addr = addr;
    frame->version = version;

    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        frame->data[i] = (uint8_t)(seed + (uint8_t)(i * 5u));
    }
}

static void reset_fixture(void) {
    memset(frame_queue, 0, sizeof(frame_queue));
    memset(frame_confirmed, 0, sizeof(frame_confirmed));
    memset(&failure, 0, sizeof(failure));
    memset(events, 0, sizeof(events));
    memset(write_records, 0, sizeof(write_records));
    memset(disk_image, 0, sizeof(disk_image));
    memset(confirmed_addr, 0, sizeof(confirmed_addr));
    memset(confirmed_version, 0, sizeof(confirmed_version));
    frame_queue_count = 0u;
    frame_queue_position = 0u;
    event_count = 0u;
    write_record_count = 0u;
    file_position = 0u;
    card_present = true;
    storage_result = MICRO_SD_RESULT_OK;
    mount_result = MICRO_SD_RESULT_OK;
    now_us = 0;
    open_count = 0u;
    seek_count = 0u;
    write_count = 0u;
    sync_count = 0u;
    close_count = 0u;
    confirm_count = 0u;
    rollback_count = 0u;
    unavailable_count = 0u;
    fatfs_reset_count = 0u;
    bus_card_absent_count = 0u;
    oled_saving_count = 0u;
    oled_ready_count = 0u;
    oled_error_count = 0u;
    busy_wait_total_ms = 0u;
    failure.write_count = PS1_FRAME_SIZE;
    copy_string(active_path, sizeof(active_path), "0:/CARD000.MCR");
    copy_string(active_name, sizeof(active_name), "CARD000.MCR");
    micro_sd_internal_worker_reset();
    micro_sd_save_worker_init(active_path);
}

void setUp(void) {
    reset_fixture();
}

void tearDown(void) {
}

FRESULT f_open(FIL *file, const char *path, BYTE mode) {
    (void)path;
    (void)mode;
    ++open_count;
    record_event(EVENT_OPEN);

    if (failure.open_result != FR_OK) {
        return failure.open_result;
    }

    if (!card_present) {
        return FR_NOT_READY;
    }

    file->object = disk_image;
    file->position = 0u;
    file->mode = mode;
    file_position = 0u;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset) {
    ++seek_count;
    record_event(EVENT_SEEK);

    if (failure.seek_result != FR_OK) {
        return failure.seek_result;
    }

    if (!card_present) {
        return FR_NOT_READY;
    }

    file->position = offset;
    file_position = offset;

    if (failure.remove_card_after_seek) {
        card_present = false;
    }

    return FR_OK;
}

FRESULT f_write(FIL *file,
                const void *data,
                UINT count,
                UINT *written) {
    ++write_count;
    record_event(EVENT_WRITE);

    if (failure.write_result != FR_OK
            && (failure.write_error_on_call == 0u
                || write_count == failure.write_error_on_call)) {
        *written = 0u;
        return failure.write_result;
    }

    if (!card_present) {
        *written = 0u;
        return FR_NOT_READY;
    }

    UINT actual = failure.write_count < count ? failure.write_count : count;
    TEST_ASSERT_TRUE(write_record_count < FRAME_QUEUE_CAPACITY);
    write_record_t *record = &write_records[write_record_count++];
    record->offset = file_position;
    record->count = actual;
    memcpy(record->data, data, actual);
    memcpy(&disk_image[(size_t)file_position], data, actual);
    file_position += actual;
    file->position = file_position;
    *written = actual;

    if (failure.remove_card_after_write) {
        card_present = false;
    }

    return FR_OK;
}

FRESULT f_sync(FIL *file) {
    (void)file;
    ++sync_count;
    record_event(EVENT_SYNC);

    if (failure.sync_result != FR_OK) {
        return failure.sync_result;
    }

    return card_present ? FR_OK : FR_NOT_READY;
}

FRESULT f_close(FIL *file) {
    ++close_count;
    record_event(EVENT_CLOSE);
    file->object = NULL;

    if (failure.remove_card_during_close) {
        card_present = false;
        return FR_NOT_READY;
    }

    if (failure.close_result != FR_OK) {
        return failure.close_result;
    }

    return card_present ? FR_OK : FR_NOT_READY;
}

FRESULT f_read(FIL *file, void *data, UINT count, UINT *read_count) {
    memcpy(data, &disk_image[(size_t)file->position], count);
    file->position += count;
    *read_count = count;
    return FR_OK;
}

FSIZE_t f_size(FIL *file) {
    (void)file;
    return PS1_CARD_SIZE;
}

const char *FRESULT_str(FRESULT result) {
    (void)result;
    return "fake FatFs result";
}

int ps1emu_take_changed_frame(uint16_t *frame_addr,
                              uint32_t *frame_version,
                              uint8_t data[PS1_FRAME_SIZE]) {
    while (frame_queue_position < frame_queue_count
            && frame_confirmed[frame_queue_position]) {
        ++frame_queue_position;
    }

    if (frame_queue_position >= frame_queue_count) {
        return TEST_PS1EMU_RESULT_NO_CHANGED_FRAME;
    }

    queued_frame_t *frame = &frame_queue[frame_queue_position++];
    *frame_addr = frame->addr;
    *frame_version = frame->version;
    memcpy(data, frame->data, PS1_FRAME_SIZE);

    if (failure.remove_card_when_frame_taken) {
        card_present = false;
    }

    return TEST_PS1EMU_RESULT_OK;
}

void ps1emu_confirm_frame_synced(uint16_t frame_addr,
                                 uint32_t frame_version) {
    TEST_ASSERT_TRUE(confirm_count < FRAME_QUEUE_CAPACITY);
    confirmed_addr[confirm_count] = frame_addr;
    confirmed_version[confirm_count] = frame_version;
    ++confirm_count;

    for (size_t i = 0u; i < frame_queue_count; ++i) {
        if (frame_queue[i].addr == frame_addr
                && frame_queue[i].version == frame_version) {
            frame_confirmed[i] = true;
            break;
        }
    }

    record_event(EVENT_CONFIRM);
}

void ps1emu_rollback_unconfirmed_frames(void) {
    ++rollback_count;
    frame_queue_position = 0u;
}

bool micro_sd_card_present(void) {
    return card_present;
}

void micro_sd_handle_card_unavailable(void) {
    ++unavailable_count;
    storage_result = MICRO_SD_ERROR_CARD_NOT_PRESENT;
    ++rollback_count;
    micro_sd_internal_worker_reset();
    ++fatfs_reset_count;
}

const char *micro_sd_active_image_path(void) {
    return active_path;
}

const char *micro_sd_active_image_name(void) {
    return active_name;
}

void micro_sd_internal_set_active_image_path(const char *path) {
    copy_string(active_path, sizeof(active_path), path);
    copy_string(active_name, sizeof(active_name), path_base_name(path));
}

micro_sd_result_t micro_sd_internal_mount(void) {
    return mount_result;
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

void ps1_bus_set_card_present(bool present) {
    if (!present) {
        ++bus_card_absent_count;
    }
}

void oled_show_saving(uint16_t frame_addr) {
    (void)frame_addr;
    ++oled_saving_count;
}

void oled_show_ready_for_image(const char *image_name) {
    (void)image_name;
    ++oled_ready_count;
}

void oled_show_sd_error(void) {
    ++oled_error_count;
}

absolute_time_t get_absolute_time(void) {
    absolute_time_t result = {.us_since_boot = now_us};
    return result;
}

int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return to.us_since_boot - from.us_since_boot;
}

void busy_wait_ms(uint32_t delay_ms) {
    busy_wait_total_ms += delay_ms;
}

void app_log_write(int level,
                   const char *module,
                   const char *format,
                   ...) {
    (void)level;
    (void)module;
    (void)format;
}

static void assert_standard_recovery(micro_sd_result_t expected_result) {
    TEST_ASSERT_EQUAL_INT(expected_result, storage_result);
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
    TEST_ASSERT_EQUAL_UINT32(1u, rollback_count);
    TEST_ASSERT_EQUAL_UINT32(1u, fatfs_reset_count);
    TEST_ASSERT_EQUAL_UINT32(1u, bus_card_absent_count);
    TEST_ASSERT_EQUAL_UINT32(1u, oled_error_count);
    TEST_ASSERT_EQUAL_UINT32(1000u, busy_wait_total_ms);
}

void test_flush_writes_one_frame_at_its_offset_then_syncs_closes_and_confirms(
        void) {
    queue_frame(17u, 9u, 0x21u);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());

    TEST_ASSERT_EQUAL_UINT32(1u, open_count);
    TEST_ASSERT_EQUAL_UINT32(1u, seek_count);
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    TEST_ASSERT_EQUAL_UINT64((FSIZE_t)17u * PS1_FRAME_SIZE,
                             write_records[0].offset);
    TEST_ASSERT_EQUAL_UINT32(PS1_FRAME_SIZE, write_records[0].count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_queue[0].data,
                                  write_records[0].data,
                                  PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    TEST_ASSERT_EQUAL_UINT32(1u, confirm_count);
    TEST_ASSERT_EQUAL_UINT16(17u, confirmed_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(9u, confirmed_version[0]);

    const test_event_t expected[] = {
            EVENT_OPEN,
            EVENT_SEEK,
            EVENT_WRITE,
            EVENT_SYNC,
            EVENT_CLOSE,
            EVENT_CONFIRM,
    };
    TEST_ASSERT_EQUAL_INT_ARRAY(expected,
                                events,
                                sizeof(expected) / sizeof(expected[0]));
}

void test_flush_writes_several_frames_and_confirms_each_after_single_sync(
        void) {
    const uint16_t addresses[] = {0u, 10u, PS1_FRAME_COUNT - 1u};

    for (size_t i = 0u; i < 3u; ++i) {
        queue_frame(addresses[i], (uint32_t)(20u + i), (uint8_t)(0x30u + i));
    }

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());

    TEST_ASSERT_EQUAL_UINT32(1u, open_count);
    TEST_ASSERT_EQUAL_UINT32(3u, write_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    TEST_ASSERT_EQUAL_UINT32(3u, confirm_count);

    for (size_t i = 0u; i < 3u; ++i) {
        TEST_ASSERT_EQUAL_UINT64((FSIZE_t)addresses[i] * PS1_FRAME_SIZE,
                                 write_records[i].offset);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_queue[i].data,
                                      write_records[i].data,
                                      PS1_FRAME_SIZE);
        TEST_ASSERT_EQUAL_UINT16(addresses[i], confirmed_addr[i]);
        TEST_ASSERT_EQUAL_UINT32(20u + i, confirmed_version[i]);
    }
}

void test_poll_syncs_only_after_250_ms_idle_delay(void) {
    queue_frame(4u, 3u, 0x41u);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);

    now_us = 249999;
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    TEST_ASSERT_EQUAL_UINT32(0u, sync_count);

    now_us = 250000;
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    TEST_ASSERT_EQUAL_UINT32(1u, confirm_count);
}

void test_flush_does_not_open_or_write_when_no_frame_changed(void) {
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(0u, open_count);
    TEST_ASSERT_EQUAL_UINT32(0u, seek_count);
    TEST_ASSERT_EQUAL_UINT32(0u, write_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0u, close_count);
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
}

void test_open_error_rolls_back_without_confirming(void) {
    queue_frame(1u, 1u, 0x51u);
    failure.open_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_OPEN_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, open_count);
    TEST_ASSERT_EQUAL_UINT32(0u, write_count);
    TEST_ASSERT_EQUAL_UINT32(0u, close_count);
    assert_standard_recovery(MICRO_SD_ERROR_OPEN_FAILED);
}

void test_seek_error_closes_file_and_rolls_back_without_confirming(void) {
    queue_frame(2u, 2u, 0x52u);
    failure.seek_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_SEEK_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, seek_count);
    TEST_ASSERT_EQUAL_UINT32(0u, write_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    assert_standard_recovery(MICRO_SD_ERROR_SEEK_FAILED);
}

void test_write_error_rolls_back_without_confirming(void) {
    queue_frame(3u, 3u, 0x53u);
    failure.write_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    assert_standard_recovery(MICRO_SD_ERROR_WRITE_FAILED);
}

void test_short_write_of_127_bytes_is_an_error_and_is_not_confirmed(void) {
    queue_frame(5u, 5u, 0x54u);
    failure.write_count = PS1_FRAME_SIZE - 1u;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(PS1_FRAME_SIZE - 1u,
                             write_records[0].count);
    assert_standard_recovery(MICRO_SD_ERROR_WRITE_FAILED);
}

void test_sync_error_closes_file_and_does_not_confirm_written_frame(void) {
    queue_frame(6u, 6u, 0x55u);
    failure.sync_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_SYNC_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    assert_standard_recovery(MICRO_SD_ERROR_SYNC_FAILED);
}

void test_close_error_does_not_confirm_synced_frame(void) {
    queue_frame(7u, 7u, 0x56u);
    failure.close_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_CLOSE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    assert_standard_recovery(MICRO_SD_ERROR_CLOSE_FAILED);
}

void test_no_space_error_from_write_is_recovered_without_confirming(void) {
    queue_frame(8u, 8u, 0x57u);
    failure.write_result = FR_DENIED;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    assert_standard_recovery(MICRO_SD_ERROR_WRITE_FAILED);
}

void test_read_only_card_error_from_open_is_recovered_without_confirming(void) {
    queue_frame(9u, 9u, 0x58u);
    failure.open_result = FR_WRITE_PROTECTED;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_OPEN_FAILED,
                          micro_sd_save_worker_flush());
    assert_standard_recovery(MICRO_SD_ERROR_OPEN_FAILED);
}

void test_card_removed_before_open_prevents_any_write(void) {
    queue_frame(11u, 11u, 0x61u);
    failure.remove_card_when_frame_taken = true;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_OPEN_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, open_count);
    TEST_ASSERT_EQUAL_UINT32(0u, write_count);
    assert_standard_recovery(MICRO_SD_ERROR_OPEN_FAILED);
}

void test_card_removed_between_open_and_write_prevents_commit(void) {
    queue_frame(12u, 12u, 0x62u);
    failure.remove_card_after_seek = true;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, open_count);
    TEST_ASSERT_EQUAL_UINT32(1u, seek_count);
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    assert_standard_recovery(MICRO_SD_ERROR_WRITE_FAILED);
}

void test_card_removed_between_write_and_sync_rolls_back_pending_frame(void) {
    queue_frame(13u, 13u, 0x63u);
    failure.remove_card_after_write = true;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_CARD_NOT_PRESENT,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, write_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
    TEST_ASSERT_EQUAL_UINT32(1u, unavailable_count);
    TEST_ASSERT_EQUAL_UINT32(1u, rollback_count);
    TEST_ASSERT_EQUAL_UINT32(1u, fatfs_reset_count);
}

void test_card_removed_during_close_does_not_confirm_frame(void) {
    queue_frame(14u, 14u, 0x64u);
    failure.remove_card_during_close = true;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_CLOSE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, close_count);
    TEST_ASSERT_FALSE(card_present);
    assert_standard_recovery(MICRO_SD_ERROR_CLOSE_FAILED);
}

void test_failed_write_is_retried_after_card_reconnect_and_then_confirmed(
        void) {
    queue_frame(21u, 31u, 0x71u);
    failure.write_result = FR_DISK_ERR;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
    TEST_ASSERT_EQUAL_UINT32(1u, rollback_count);

    card_present = false;
    card_present = true;
    failure.write_result = FR_OK;
    micro_sd_save_worker_init(active_path);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(2u, write_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(1u, confirm_count);
    TEST_ASSERT_EQUAL_UINT16(21u, confirmed_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(31u, confirmed_version[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            frame_queue[0].data,
            &disk_image[(size_t)21u * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
}

void test_reconnect_retries_several_frames_left_pending_by_failure(void) {
    const uint16_t addresses[] = {2u, 22u, 222u};

    for (size_t i = 0u; i < 3u; ++i) {
        queue_frame(addresses[i],
                    (uint32_t)(40u + i),
                    (uint8_t)(0x80u + i));
    }

    failure.write_result = FR_DISK_ERR;
    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
    TEST_ASSERT_EQUAL_UINT32(1u, rollback_count);

    failure.write_result = FR_OK;
    card_present = true;
    micro_sd_save_worker_init(active_path);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(4u, write_count);
    TEST_ASSERT_EQUAL_UINT32(3u, write_record_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(3u, confirm_count);

    for (size_t i = 0u; i < 3u; ++i) {
        TEST_ASSERT_EQUAL_UINT16(addresses[i], confirmed_addr[i]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
                frame_queue[i].data,
                &disk_image[(size_t)addresses[i] * PS1_FRAME_SIZE],
                PS1_FRAME_SIZE);
    }
}

void test_failure_after_partial_batch_replays_all_unconfirmed_frames(void) {
    const uint16_t addresses[] = {3u, 33u, 333u, 999u};

    for (size_t i = 0u; i < 4u; ++i) {
        queue_frame(addresses[i],
                    (uint32_t)(50u + i),
                    (uint8_t)(0x90u + i));
    }

    failure.write_result = FR_DISK_ERR;
    failure.write_error_on_call = 3u;

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(3u, write_count);
    TEST_ASSERT_EQUAL_UINT32(2u, write_record_count);
    TEST_ASSERT_EQUAL_UINT32(0u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(0u, confirm_count);
    TEST_ASSERT_EQUAL_UINT32(1u, rollback_count);

    failure.write_result = FR_OK;
    failure.write_error_on_call = 0u;
    card_present = true;
    micro_sd_save_worker_init(active_path);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(7u, write_count);
    TEST_ASSERT_EQUAL_UINT32(6u, write_record_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sync_count);
    TEST_ASSERT_EQUAL_UINT32(4u, confirm_count);

    for (size_t i = 0u; i < 4u; ++i) {
        TEST_ASSERT_EQUAL_UINT16(addresses[i], confirmed_addr[i]);
        TEST_ASSERT_EQUAL_UINT32(50u + i, confirmed_version[i]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
                frame_queue[i].data,
                &disk_image[(size_t)addresses[i] * PS1_FRAME_SIZE],
                PS1_FRAME_SIZE);
    }
}
