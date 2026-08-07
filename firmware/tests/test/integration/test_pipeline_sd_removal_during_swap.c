#include "unity.h"

#include "micro_sd/micro_sd_image.h"
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

void test_pipeline_sd_removal_during_swap_never_exposes_partial_image(
        void) {
    const uint16_t read_addr = 124u;
    uint8_t data_a[PS1_FRAME_SIZE];
    uint8_t data_b[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);
    uint8_t *image_b = pipeline_add_valid_image(PIPELINE_IMAGE_B_PATH);

    pipeline_fill_pattern(data_a, 0xA1u);
    pipeline_fill_pattern(data_b, 0xB1u);
    memcpy(&image_a[(size_t)read_addr * PS1_FRAME_SIZE],
           data_a,
           PS1_FRAME_SIZE);
    memcpy(&image_b[(size_t)read_addr * PS1_FRAME_SIZE],
           data_b,
           PS1_FRAME_SIZE);
    pipeline_initialize_image_a();

    pipeline_fail_next_read_partially(PIPELINE_IMAGE_B_PATH,
                                      PS1_CARD_SIZE / 2u);
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_ERROR_READ_FAILED,
            micro_sd_activate_image_as_inserted_card("CARD_B.MCR"));
    TEST_ASSERT_FALSE(pipeline_physical_card_present());
    TEST_ASSERT_EQUAL_STRING(PIPELINE_IMAGE_A_PATH,
                             micro_sd_active_image_path());

    pipeline_prepare_read(read_addr);
    TEST_ASSERT_FALSE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_script_position());

    pipeline_set_physical_card_present(true);
    TEST_ASSERT_EQUAL_INT(
            MICRO_SD_RESULT_OK,
            micro_sd_activate_image_as_inserted_card("CARD_B.MCR"));
    TEST_ASSERT_EQUAL_STRING(PIPELINE_IMAGE_B_PATH,
                             micro_sd_active_image_path());

    pipeline_finish_card_swap_delay();
    pipeline_prepare_read(read_addr);
    TEST_ASSERT_TRUE(pipeline_run_console_transaction());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data_b,
                                  pipeline_read_response_data(),
                                  PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(image_b,
                                  card_image,
                                  PS1_CARD_SIZE);
}
