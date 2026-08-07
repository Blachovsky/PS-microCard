#include "unity.h"

#include "ff.h"
#include "micro_sd/micro_sd_image.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_FILES 1100u
#define FAKE_PATH_MAX  300u

#define PS1_DIR_FRAME_COUNT  16u
#define PS1_DIR_CHECKSUM_POS 127u
#define PS1_DIR_FREE         0xA0u

typedef struct {
    bool used;
    bool directory;
    char path[FAKE_PATH_MAX];
    FSIZE_t size;
    uint8_t fill;
    uint8_t *content;
    size_t capacity;
} fake_file_t;

static fake_file_t fake_files[FAKE_MAX_FILES];
uint8_t card_image[PS1_CARD_SIZE];

static micro_sd_result_t mount_result;
static micro_sd_result_t flush_result;
static char active_path[MICRO_SD_IMAGE_PATH_MAX];
static char active_name[MICRO_SD_IMAGE_NAME_MAX];
static char worker_path[MICRO_SD_IMAGE_PATH_MAX];
static uint32_t pause_request_count;
static uint32_t pause_release_count;
static uint32_t swap_count;
static uint32_t storage_init_count;
static uint32_t worker_init_count;
static uint32_t flush_count;

static void fake_fs_reset(void) {
    for (size_t i = 0; i < FAKE_MAX_FILES; ++i) {
        free(fake_files[i].content);
    }

    memset(fake_files, 0, sizeof(fake_files));
}

static fake_file_t *fake_find_file(const char *path) {
    for (size_t i = 0; i < FAKE_MAX_FILES; ++i) {
        if (fake_files[i].used && strcmp(fake_files[i].path, path) == 0) {
            return &fake_files[i];
        }
    }

    return NULL;
}

static fake_file_t *fake_add_file(const char *path,
                                  FSIZE_t size,
                                  uint8_t fill,
                                  bool materialize) {
    TEST_ASSERT_NULL(fake_find_file(path));

    for (size_t i = 0; i < FAKE_MAX_FILES; ++i) {
        fake_file_t *file = &fake_files[i];

        if (file->used) {
            continue;
        }

        memset(file, 0, sizeof(*file));
        file->used = true;
        file->size = size;
        file->fill = fill;
        (void)snprintf(file->path, sizeof(file->path), "%s", path);

        if (materialize && size > 0u) {
            TEST_ASSERT_TRUE(size <= SIZE_MAX);
            file->content = malloc((size_t)size);
            TEST_ASSERT_NOT_NULL(file->content);
            memset(file->content, fill, (size_t)size);
            file->capacity = (size_t)size;
        }

        return file;
    }

    TEST_FAIL_MESSAGE("Fake filesystem is full");
    return NULL;
}

static size_t fake_file_count(void) {
    size_t count = 0u;

    for (size_t i = 0; i < FAKE_MAX_FILES; ++i) {
        if (fake_files[i].used) {
            ++count;
        }
    }

    return count;
}

static uint8_t directory_checksum(const uint8_t frame[PS1_FRAME_SIZE]) {
    uint8_t checksum = 0u;

    for (size_t i = 0; i < PS1_DIR_CHECKSUM_POS; ++i) {
        checksum ^= frame[i];
    }

    return checksum;
}

static void update_directory_checksum(uint8_t frame[PS1_FRAME_SIZE]) {
    frame[PS1_DIR_CHECKSUM_POS] = directory_checksum(frame);
}

static void format_valid_blank_image(uint8_t image[PS1_CARD_SIZE]) {
    memset(image, 0, PS1_CARD_SIZE);
    image[0] = 'M';
    image[1] = 'C';
    update_directory_checksum(&image[0]);

    for (size_t slot = 1u; slot < PS1_DIR_FRAME_COUNT; ++slot) {
        uint8_t *entry = &image[slot * PS1_FRAME_SIZE];
        entry[0] = PS1_DIR_FREE;
        entry[8] = 0xFFu;
        entry[9] = 0xFFu;
        update_directory_checksum(entry);
    }
}

static fake_file_t *fake_add_valid_image(const char *path) {
    fake_file_t *file = fake_add_file(path,
                                      PS1_CARD_SIZE,
                                      0u,
                                      true);
    format_valid_blank_image(file->content);
    return file;
}

static const char *base_name(const char *path) {
    const char *base = path;

    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            base = p + 1;
        }
    }

    return base;
}

static bool ensure_capacity(fake_file_t *file, size_t required) {
    if (required <= file->capacity) {
        return true;
    }

    size_t capacity = file->capacity == 0u ? 256u : file->capacity;
    while (capacity < required) {
        capacity *= 2u;
    }

    uint8_t *content = realloc(file->content, capacity);
    if (content == NULL) {
        return false;
    }

    memset(&content[file->capacity], 0, capacity - file->capacity);
    file->content = content;
    file->capacity = capacity;
    return true;
}

FRESULT f_stat(const char *path, FILINFO *info) {
    fake_file_t *file = fake_find_file(path);

    if (file == NULL) {
        return FR_NO_FILE;
    }

    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->fsize = file->size;
        info->fattrib = file->directory ? AM_DIR : 0u;
        (void)snprintf(info->fname, sizeof(info->fname), "%s", base_name(path));
    }

    return FR_OK;
}

FRESULT f_open(FIL *file, const char *path, BYTE mode) {
    fake_file_t *entry = fake_find_file(path);

    if ((mode & FA_CREATE_NEW) != 0u) {
        if (entry != NULL) {
            return FR_EXIST;
        }

        entry = fake_add_file(path, 0u, 0u, false);
    } else if (entry == NULL) {
        return FR_NO_FILE;
    }

    if (entry->directory) {
        return FR_DENIED;
    }

    file->object = entry;
    file->position = 0u;
    file->mode = mode;
    return FR_OK;
}

FRESULT f_read(FIL *file, void *data, UINT count, UINT *read_count) {
    fake_file_t *entry = file->object;
    FSIZE_t available = file->position < entry->size
            ? entry->size - file->position
            : 0u;
    UINT actual = available < count ? (UINT)available : count;

    if (entry->content != NULL) {
        memcpy(data, &entry->content[(size_t)file->position], actual);
    } else {
        memset(data, entry->fill, actual);
    }

    file->position += actual;
    *read_count = actual;
    return FR_OK;
}

FRESULT f_write(FIL *file,
                const void *data,
                UINT count,
                UINT *write_count) {
    fake_file_t *entry = file->object;
    FSIZE_t end = file->position + count;

    if (end > SIZE_MAX || !ensure_capacity(entry, (size_t)end)) {
        *write_count = 0u;
        return FR_DISK_ERR;
    }

    memcpy(&entry->content[(size_t)file->position], data, count);
    file->position = end;
    if (end > entry->size) {
        entry->size = end;
    }

    *write_count = count;
    return FR_OK;
}

FRESULT f_lseek(FIL *file, FSIZE_t offset) {
    file->position = offset;
    return FR_OK;
}

FRESULT f_sync(FIL *file) {
    (void)file;
    return FR_OK;
}

FRESULT f_close(FIL *file) {
    file->object = NULL;
    return FR_OK;
}

FRESULT f_unlink(const char *path) {
    fake_file_t *file = fake_find_file(path);

    if (file == NULL) {
        return FR_NO_FILE;
    }

    free(file->content);
    memset(file, 0, sizeof(*file));
    return FR_OK;
}

FRESULT f_opendir(DIR *dir, const char *path) {
    (void)path;
    dir->index = 0u;
    return FR_OK;
}

FRESULT f_readdir(DIR *dir, FILINFO *info) {
    while (dir->index < FAKE_MAX_FILES) {
        fake_file_t *file = &fake_files[dir->index++];

        if (!file->used) {
            continue;
        }

        memset(info, 0, sizeof(*info));
        info->fsize = file->size;
        info->fattrib = file->directory ? AM_DIR : 0u;
        (void)snprintf(info->fname,
                       sizeof(info->fname),
                       "%s",
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

bool micro_sd_internal_names_equal_ignore_case(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (micro_sd_internal_ascii_upper(*a++) !=
            micro_sd_internal_ascii_upper(*b++)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

void micro_sd_internal_copy_string(char *dst,
                                   size_t dst_size,
                                   const char *src) {
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        src = "";
    }

    (void)snprintf(dst, dst_size, "%s", src);
}

void micro_sd_internal_copy_name_from_path(const char *path,
                                           char *name,
                                           size_t name_size) {
    micro_sd_internal_copy_string(name,
                                  name_size,
                                  path == NULL ? "" : base_name(path));
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
    micro_sd_internal_copy_string(active_path, sizeof(active_path), path);
    micro_sd_internal_copy_name_from_path(path,
                                          active_name,
                                          sizeof(active_name));
}

micro_sd_result_t micro_sd_internal_mount(void) {
    return mount_result;
}

bool micro_sd_is_active_image(const char *image_name) {
    char name[MICRO_SD_IMAGE_NAME_MAX];

    micro_sd_internal_copy_name_from_path(image_name, name, sizeof(name));
    return micro_sd_internal_names_equal_ignore_case(name, active_name);
}

micro_sd_result_t micro_sd_save_worker_flush(void) {
    ++flush_count;
    return flush_result;
}

void micro_sd_save_worker_init(const char *path) {
    ++worker_init_count;
    micro_sd_internal_copy_string(worker_path, sizeof(worker_path), path);
}

void ps1_bus_request_pause_blocking(void) {
    ++pause_request_count;
}

void ps1_bus_release_pause(void) {
    ++pause_release_count;
}

void ps1_bus_begin_card_swap_absent(void) {
    ++swap_count;
}

void ps1_bus_set_card_present(bool present) {
    (void)present;
}

void ps1emu_storage_state_init(void) {
    ++storage_init_count;
}

void app_log_write(int level,
                   const char *module,
                   const char *format,
                   ...) {
    (void)level;
    (void)module;
    (void)format;
}

static void reset_fixture(void) {
    fake_fs_reset();
    memset(card_image, 0xCC, sizeof(card_image));
    memset(active_path, 0, sizeof(active_path));
    memset(active_name, 0, sizeof(active_name));
    memset(worker_path, 0, sizeof(worker_path));
    micro_sd_internal_set_active_image_path("0:/ACTIVE.MCR");
    mount_result = MICRO_SD_RESULT_OK;
    flush_result = MICRO_SD_RESULT_OK;
    pause_request_count = 0u;
    pause_release_count = 0u;
    swap_count = 0u;
    storage_init_count = 0u;
    worker_init_count = 0u;
    flush_count = 0u;
}

void setUp(void) {
    reset_fixture();
}

void tearDown(void) {
    fake_fs_reset();
}

void test_create_new_mcr_has_exact_size_header_directory_and_checksums(void) {
    const char *path = "0:/CARD000.MCR";

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_load_or_create_initial_image(path));

    fake_file_t *file = fake_find_file(path);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT64(PS1_CARD_SIZE, file->size);
    TEST_ASSERT_EQUAL_HEX8('M', file->content[0]);
    TEST_ASSERT_EQUAL_HEX8('C', file->content[1]);
    TEST_ASSERT_EQUAL_HEX8(directory_checksum(&file->content[0]),
                           file->content[PS1_DIR_CHECKSUM_POS]);

    for (size_t slot = 1u; slot < PS1_DIR_FRAME_COUNT; ++slot) {
        uint8_t *entry = &file->content[slot * PS1_FRAME_SIZE];
        TEST_ASSERT_EQUAL_HEX8(PS1_DIR_FREE, entry[0]);
        TEST_ASSERT_EQUAL_HEX8(0xFFu, entry[8]);
        TEST_ASSERT_EQUAL_HEX8(0xFFu, entry[9]);
        TEST_ASSERT_EQUAL_HEX8(directory_checksum(entry),
                               entry[PS1_DIR_CHECKSUM_POS]);
    }

    TEST_ASSERT_EQUAL_UINT8_ARRAY(file->content,
                                  card_image,
                                  PS1_CARD_SIZE);
    TEST_ASSERT_EQUAL_STRING(path, active_path);
}

void test_image_size_boundaries_accept_only_131072_bytes(void) {
    static const FSIZE_t invalid_sizes[] = {
            0u,
            1u,
            PS1_CARD_SIZE - 1u,
            PS1_CARD_SIZE + 1u,
            UINT64_C(1) << 40,
    };

    for (size_t i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]);
         ++i) {
        reset_fixture();
        (void)fake_add_file("0:/SIZE.MCR", invalid_sizes[i], 0u, false);

        TEST_ASSERT_EQUAL_INT(
                MICRO_SD_ERROR_INVALID_IMAGE_SIZE,
                micro_sd_load_or_create_initial_image("0:/SIZE.MCR"));
    }

    reset_fixture();
    (void)fake_add_valid_image("0:/SIZE.MCR");
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_load_or_create_initial_image("0:/SIZE.MCR"));
}

void test_load_existing_valid_image_copies_all_bytes_to_card_ram(void) {
    fake_file_t *file = fake_add_valid_image("0:/EXISTING.MCR");

    for (size_t i = PS1_DIR_FRAME_COUNT * PS1_FRAME_SIZE;
         i < PS1_CARD_SIZE;
         ++i) {
        file->content[i] = (uint8_t)(i * 7u);
    }

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_load_or_create_initial_image("0:/EXISTING.MCR"));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(file->content,
                                  card_image,
                                  PS1_CARD_SIZE);
    TEST_ASSERT_EQUAL_STRING("EXISTING.MCR", active_name);
}

void test_all_zero_and_all_ff_images_are_formatted_as_blank_cards(void) {
    const uint8_t erased_values[] = {0x00u, 0xFFu};

    for (size_t i = 0; i < sizeof(erased_values); ++i) {
        reset_fixture();
        fake_file_t *file = fake_add_file("0:/ERASED.MCR",
                                          PS1_CARD_SIZE,
                                          erased_values[i],
                                          true);

        TEST_ASSERT_EQUAL_INT(
                MICRO_SD_RESULT_OK,
                micro_sd_load_or_create_initial_image("0:/ERASED.MCR"));
        TEST_ASSERT_EQUAL_HEX8('M', file->content[0]);
        TEST_ASSERT_EQUAL_HEX8('C', file->content[1]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(file->content,
                                      card_image,
                                      PS1_CARD_SIZE);
    }
}

void test_invalid_header_directory_state_and_directory_checksum_are_rejected(
        void) {
    fake_file_t *file = fake_add_valid_image("0:/BAD.MCR");
    file->content[0] = 'X';
    update_directory_checksum(&file->content[0]);

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_ERROR_INVALID_IMAGE_FORMAT,
            micro_sd_load_or_create_initial_image("0:/BAD.MCR"));

    reset_fixture();
    file = fake_add_valid_image("0:/BAD.MCR");
    uint8_t *entry = &file->content[3u * PS1_FRAME_SIZE];
    entry[0] = 0x99u;
    update_directory_checksum(entry);

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_ERROR_INVALID_IMAGE_FORMAT,
            micro_sd_load_or_create_initial_image("0:/BAD.MCR"));

    reset_fixture();
    file = fake_add_valid_image("0:/BAD.MCR");
    file->content[5u * PS1_FRAME_SIZE + PS1_DIR_CHECKSUM_POS] ^= 0x01u;

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_ERROR_INVALID_IMAGE_FORMAT,
            micro_sd_load_or_create_initial_image("0:/BAD.MCR"));
}

void test_deleted_directory_entry_states_are_accepted(void) {
    static const uint8_t deleted_states[] = {0xA1u, 0xA2u, 0xA3u};
    fake_file_t *file = fake_add_valid_image("0:/DELETED.MCR");

    for (size_t i = 0u; i < sizeof(deleted_states); ++i) {
        uint8_t *entry = &file->content[(i + 1u) * PS1_FRAME_SIZE];
        entry[0] = deleted_states[i];
        update_directory_checksum(entry);
    }

    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_load_or_create_initial_image("0:/DELETED.MCR"));
}

void test_list_images_accepts_mcr_case_variants_and_filters_other_files(void) {
    micro_sd_image_entry_t entries[8];

    (void)fake_add_file("0:/CARD003.MCR", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/CARD001.mcr", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/CARD002.McR", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/NO_EXTENSION", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/NOT_IMAGE.txt", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/WRONG_SIZE.MCR", PS1_CARD_SIZE - 1u, 0u, false);
    fake_file_t *directory =
            fake_add_file("0:/DIRECTORY.MCR", PS1_CARD_SIZE, 0u, false);
    directory->directory = true;
    (void)fake_add_file(
            "0:/THIS_FILE_NAME_IS_FAR_TOO_LONG_FOR_THE_IMAGE_API.MCR",
            PS1_CARD_SIZE,
            0u,
            false);

    TEST_ASSERT_EQUAL_UINT32(3u, micro_sd_list_images(entries, 8u));
    TEST_ASSERT_EQUAL_STRING("CARD001.mcr", entries[0].name);
    TEST_ASSERT_EQUAL_STRING("CARD002.McR", entries[1].name);
    TEST_ASSERT_EQUAL_STRING("CARD003.MCR", entries[2].name);
}

void test_delete_image_flushes_storage_and_removes_file(void) {
    (void)fake_add_valid_image("0:/DELETE.MCR");

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_delete_image("DELETE.MCR"));
    TEST_ASSERT_NULL(fake_find_file("0:/DELETE.MCR"));
    TEST_ASSERT_EQUAL_UINT32(1u, pause_request_count);
    TEST_ASSERT_EQUAL_UINT32(1u, flush_count);
    TEST_ASSERT_EQUAL_UINT32(1u, pause_release_count);
}

void test_create_auto_uses_first_free_card_number(void) {
    char created_name[MICRO_SD_IMAGE_NAME_MAX];

    (void)fake_add_file("0:/CARD000.MCR", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/CARD001.MCR", PS1_CARD_SIZE, 0u, false);
    (void)fake_add_file("0:/CARD003.MCR", PS1_CARD_SIZE, 0u, false);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_create_blank_image_auto(created_name));
    TEST_ASSERT_EQUAL_STRING("CARD002.MCR", created_name);

    fake_file_t *created = fake_find_file("0:/CARD002.MCR");
    TEST_ASSERT_NOT_NULL(created);
    TEST_ASSERT_EQUAL_UINT64(PS1_CARD_SIZE, created->size);
    TEST_ASSERT_EQUAL_HEX8('M', created->content[0]);
    TEST_ASSERT_EQUAL_HEX8('C', created->content[1]);
}

void test_list_images_stops_at_capacity_when_more_images_exist(void) {
    struct {
        micro_sd_image_entry_t entries[MICRO_SD_MAX_IMAGES];
        uint32_t canary;
    } output;
    char path[32];

    memset(&output, 0, sizeof(output));
    output.canary = UINT32_C(0xA55A1234);

    for (unsigned i = 0u; i < MICRO_SD_MAX_IMAGES; ++i) {
        (void)snprintf(path, sizeof(path), "0:/IMAGE%03u.MCR", i);
        (void)fake_add_file(path, PS1_CARD_SIZE, 0u, false);
    }

    TEST_ASSERT_EQUAL_UINT32(
            MICRO_SD_MAX_IMAGES,
            micro_sd_list_images(output.entries, MICRO_SD_MAX_IMAGES));

    for (unsigned i = MICRO_SD_MAX_IMAGES;
         i < MICRO_SD_MAX_IMAGES + 6u;
         ++i) {
        (void)snprintf(path, sizeof(path), "0:/IMAGE%03u.MCR", i);
        (void)fake_add_file(path, PS1_CARD_SIZE, 0u, false);
    }

    TEST_ASSERT_EQUAL_UINT32(
            MICRO_SD_MAX_IMAGES,
            micro_sd_list_images(output.entries, MICRO_SD_MAX_IMAGES));
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0xA55A1234), output.canary);
}

void test_create_auto_reports_error_when_card000_through_card999_are_taken(
        void) {
    char path[32];
    char created_name[MICRO_SD_IMAGE_NAME_MAX] = "unchanged";

    for (unsigned i = 0u; i <= 999u; ++i) {
        (void)snprintf(path, sizeof(path), "0:/CARD%03u.MCR", i);
        (void)fake_add_file(path, PS1_CARD_SIZE, 0u, false);
    }

    TEST_ASSERT_EQUAL_UINT32(1000u, fake_file_count());
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_ERROR_NO_FREE_IMAGE_NAME,
            micro_sd_create_blank_image_auto(created_name));
    TEST_ASSERT_EQUAL_STRING("", created_name);
    TEST_ASSERT_EQUAL_UINT32(1000u, fake_file_count());
}
