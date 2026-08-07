#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"
#include "ps1/ps1_card_bus.h"

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

void test_pipeline_write_failure_retries_dirty_frame_after_sd_recovers(
        void) {
    const uint16_t frame_addr = 121u;
    uint8_t new_data[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_fill_pattern(new_data, 0x41u);
    pipeline_initialize_image_a();
    pipeline_console_write(frame_addr, new_data);
    pipeline_fail_next_write();

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_WRITE_FAILED,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_sync_count());
    pipeline_assert_frame_is_dirty(frame_addr, new_data);

    pipeline_set_physical_card_present(false);
    pipeline_set_physical_card_present(true);
    micro_sd_save_worker_init(PIPELINE_IMAGE_A_PATH);
    ps1_bus_set_card_present(true);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(2u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            new_data,
            &image_a[(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    pipeline_assert_no_dirty_frames();
}
