#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"

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

void test_pipeline_restart_before_sync_uses_only_last_confirmed_data(void) {
    const uint16_t frame_addr = 202u;
    uint8_t old_data[PS1_FRAME_SIZE];
    uint8_t new_data[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    memcpy(old_data,
           &image_a[(size_t)frame_addr * PS1_FRAME_SIZE],
           PS1_FRAME_SIZE);
    pipeline_fill_pattern(new_data, 0x42u);
    pipeline_initialize_image_a();
    pipeline_console_write(frame_addr, new_data);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_poll());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_sync_count());
    pipeline_assert_frame_is_dirty(frame_addr, new_data);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            old_data,
            &pipeline_durable_image_data(PIPELINE_IMAGE_A_PATH)
                    [(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);

    pipeline_restart_firmware(PIPELINE_IMAGE_A_PATH);
    pipeline_prepare_read(frame_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(old_data,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);
    pipeline_assert_no_dirty_frames();
}
