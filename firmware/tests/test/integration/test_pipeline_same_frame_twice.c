#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"
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

void test_pipeline_second_write_to_same_frame_wins_before_sync(void) {
    const uint16_t frame_addr = 203u;
    uint8_t data_a[PS1_FRAME_SIZE];
    uint8_t data_b[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_fill_pattern(data_a, 0x52u);
    pipeline_fill_pattern(data_b, 0x62u);
    pipeline_initialize_image_a();

    pipeline_console_write(frame_addr, data_a);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_sync_count());

    pipeline_console_write(frame_addr, data_b);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(2u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            data_b,
            &card_image[(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            data_b,
            &image_a[(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            data_b,
            &pipeline_durable_image_data(PIPELINE_IMAGE_A_PATH)
                    [(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    pipeline_assert_no_dirty_frames();
}
