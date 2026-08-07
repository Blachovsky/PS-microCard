#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"
#include "ps1/ps1_card_emulator.h"

#include <string.h>

TEST_SOURCE_FILE("test/integration/support/pipeline_test_support.c")
TEST_SOURCE_FILE("../micro_sd/micro_sd_image.c")
TEST_SOURCE_FILE("../micro_sd/micro_sd_worker.c")
TEST_SOURCE_FILE("../ps1/ps1_card_bus.c")
TEST_SOURCE_FILE("../ps1/ps1_card_emulator.c")

void setUp(void) {
    pipeline_test_set_up();
}

void tearDown(void) {
    pipeline_test_tear_down();
}

void test_pipeline_sd_removal_during_write_disconnects_ps1_safely(void) {
    const uint16_t frame_addr = 205u;
    uint8_t old_data[PS1_FRAME_SIZE];
    uint8_t first_data[PS1_FRAME_SIZE];
    uint8_t ignored_data[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    memcpy(old_data,
           &image_a[(size_t)frame_addr * PS1_FRAME_SIZE],
           PS1_FRAME_SIZE);
    pipeline_fill_pattern(first_data, 0x83u);
    pipeline_fill_pattern(ignored_data, 0x93u);
    pipeline_initialize_image_a();
    pipeline_console_write(frame_addr, first_data);
    pipeline_remove_sd_during_next_write(PS1_FRAME_SIZE / 2u);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_FALSE(pipeline_physical_card_present());
    TEST_ASSERT_EQUAL_UINT32(PS1_FRAME_SIZE / 2u,
                             pipeline_last_write_size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            old_data,
            &pipeline_durable_image_data(PIPELINE_IMAGE_A_PATH)
                    [(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);

    pipeline_prepare_write(frame_addr, ignored_data);
    TEST_ASSERT_FALSE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_script_position());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            first_data,
            &card_image[(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    pipeline_assert_frame_is_dirty(frame_addr, first_data);
}
