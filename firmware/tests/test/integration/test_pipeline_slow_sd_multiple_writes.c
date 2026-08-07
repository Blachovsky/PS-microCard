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

void test_pipeline_slow_sd_converges_to_latest_ram_state_per_frame(void) {
    const uint16_t addresses[] = {5u, 100u, 500u};
    uint8_t data_5_a[PS1_FRAME_SIZE];
    uint8_t data_5_b[PS1_FRAME_SIZE];
    uint8_t data_100_a[PS1_FRAME_SIZE];
    uint8_t data_100_b[PS1_FRAME_SIZE];
    uint8_t data_500[PS1_FRAME_SIZE];
    const uint8_t *expected[3];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_fill_pattern(data_5_a, 0x12u);
    pipeline_fill_pattern(data_5_b, 0x22u);
    pipeline_fill_pattern(data_100_a, 0x32u);
    pipeline_fill_pattern(data_100_b, 0x42u);
    pipeline_fill_pattern(data_500, 0x52u);
    expected[0] = data_5_b;
    expected[1] = data_100_b;
    expected[2] = data_500;
    pipeline_initialize_image_a();
    pipeline_set_storage_delays(100000, 300000);

    pipeline_console_write(5u, data_5_a);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    pipeline_console_write(100u, data_100_a);
    pipeline_console_write(5u, data_5_b);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    pipeline_console_write(500u, data_500);
    pipeline_console_write(100u, data_100_b);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(5u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());

    for (size_t i = 0u; i < 3u; ++i) {
        size_t offset = (size_t)addresses[i] * PS1_FRAME_SIZE;
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i],
                                      &card_image[offset],
                                      PS1_FRAME_SIZE);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected[i],
                                      &image_a[offset],
                                      PS1_FRAME_SIZE);
    }

    pipeline_assert_no_dirty_frames();
}
