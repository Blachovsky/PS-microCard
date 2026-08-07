#include "unity.h"

#include "micro_sd/micro_sd_image.h"
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

void test_pipeline_image_swap_flushes_a_and_exposes_only_b_after_delay(
        void) {
    const uint16_t read_addr = 123u;
    const uint16_t write_addr = 701u;
    uint8_t data_a[PS1_FRAME_SIZE];
    uint8_t data_b[PS1_FRAME_SIZE];
    uint8_t pending_a[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);
    uint8_t *image_b = pipeline_add_valid_image(PIPELINE_IMAGE_B_PATH);

    pipeline_fill_pattern(data_a, 0x71u);
    pipeline_fill_pattern(data_b, 0x81u);
    pipeline_fill_pattern(pending_a, 0x91u);
    memcpy(&image_a[(size_t)read_addr * PS1_FRAME_SIZE],
           data_a,
           PS1_FRAME_SIZE);
    memcpy(&image_b[(size_t)read_addr * PS1_FRAME_SIZE],
           data_b,
           PS1_FRAME_SIZE);
    pipeline_initialize_image_a();

    pipeline_prepare_read(read_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_a,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);

    pipeline_console_write(write_addr, pending_a);
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_activate_image_as_inserted_card("CARD_B.MCR"));
    TEST_ASSERT_EQUAL_STRING(PIPELINE_IMAGE_B_PATH,
                             micro_sd_active_image_path());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            pending_a,
            &image_a[(size_t)write_addr * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());

    pipeline_prepare_read(read_addr);
    TEST_ASSERT_FALSE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_script_position());
    pipeline_advance_time_us(1500 * 1000);
    pipeline_prepare_read(read_addr);
    TEST_ASSERT_FALSE(pipeline_run_console_transaction());
    pipeline_prepare_read(read_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_b,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);
    TEST_ASSERT_FALSE(memcmp(data_a,
                             pipeline_read_response_data(),
                             PS1_FRAME_SIZE) == 0);
}
