#include "unity.h"

#include "micro_sd/micro_sd_worker.h"
#include "pipeline_test_support.h"

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

void test_pipeline_normal_write_is_synced_confirmed_and_persisted(void) {
    const uint16_t frame_addr = 120u;
    uint8_t new_data[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_fill_pattern(new_data, 0x31u);
    pipeline_initialize_image_a();
    pipeline_console_write(frame_addr, new_data);
    pipeline_assert_frame_is_dirty(frame_addr, new_data);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());
    TEST_ASSERT_EQUAL_UINT64((FSIZE_t)frame_addr * PS1_FRAME_SIZE,
                             pipeline_last_write_offset());
    TEST_ASSERT_EQUAL_UINT32(PS1_FRAME_SIZE,
                             pipeline_last_write_size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            new_data,
            &image_a[(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    pipeline_assert_no_dirty_frames();

    pipeline_prepare_read(frame_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(new_data,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);
}
