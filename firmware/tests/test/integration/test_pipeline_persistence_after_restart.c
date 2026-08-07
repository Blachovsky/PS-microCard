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

void test_pipeline_synced_write_survives_complete_firmware_restart(void) {
    const uint16_t frame_addr = 201u;
    uint8_t new_data[PS1_FRAME_SIZE];

    (void)pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);
    pipeline_fill_pattern(new_data, 0x32u);
    pipeline_initialize_image_a();
    pipeline_console_write(frame_addr, new_data);
    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());

    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            new_data,
            &pipeline_durable_image_data(PIPELINE_IMAGE_A_PATH)
                    [(size_t)frame_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);

    pipeline_restart_firmware(PIPELINE_IMAGE_A_PATH);
    pipeline_prepare_read(frame_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(new_data,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);
}
