#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"

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

void test_pipeline_failure_on_third_frame_recovers_entire_batch(void) {
    const uint16_t addresses[] = {10u, 11u, 12u, 13u};
    uint8_t data[4][PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_initialize_image_a();
    for (size_t i = 0u; i < 4u; ++i) {
        pipeline_fill_pattern(data[i], (uint8_t)(0x63u + i * 0x10u));
        pipeline_console_write(addresses[i], data[i]);
    }

    pipeline_fail_write_on_call(3u);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(3u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_sync_count());
    pipeline_assert_frame_is_dirty(addresses[0], data[0]);

    pipeline_reconnect_storage();
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(7u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());

    for (size_t i = 0u; i < 4u; ++i) {
        size_t offset = (size_t)addresses[i] * PS1_FRAME_SIZE;
        TEST_ASSERT_EQUAL_UINT8_ARRAY(data[i],
                                      &card_image[offset],
                                      PS1_FRAME_SIZE);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(data[i],
                                      &image_a[offset],
                                      PS1_FRAME_SIZE);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
                data[i],
                &pipeline_durable_image_data(PIPELINE_IMAGE_A_PATH)
                        [offset],
                PS1_FRAME_SIZE);
    }

    pipeline_assert_no_dirty_frames();
}
