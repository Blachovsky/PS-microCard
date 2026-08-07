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

void test_pipeline_console_writes_during_sd_outage_sync_after_reconnect(
        void) {
    const uint16_t addresses[] = {122u, 700u};
    uint8_t first_data[PS1_FRAME_SIZE];
    uint8_t second_data[PS1_FRAME_SIZE];
    uint8_t *image_a = pipeline_add_valid_image(PIPELINE_IMAGE_A_PATH);

    pipeline_fill_pattern(first_data, 0x51u);
    pipeline_fill_pattern(second_data, 0x61u);
    pipeline_initialize_image_a();
    pipeline_set_physical_card_present(false);

    pipeline_console_write(addresses[0], first_data);
    pipeline_console_write(addresses[1], second_data);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            first_data,
            &card_image[(size_t)addresses[0] * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            second_data,
            &card_image[(size_t)addresses[1] * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_ERROR_CARD_NOT_PRESENT,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(0u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_fatfs_reset_count());

    pipeline_set_physical_card_present(true);
    micro_sd_save_worker_init(PIPELINE_IMAGE_A_PATH);
    ps1_bus_set_card_present(true);

    TEST_ASSERT_EQUAL_INT(MICRO_SD_RESULT_OK,
                          micro_sd_save_worker_flush());
    TEST_ASSERT_EQUAL_UINT32(2u, pipeline_write_count());
    TEST_ASSERT_EQUAL_UINT32(1u, pipeline_sync_count());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            first_data,
            &image_a[(size_t)addresses[0] * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
            second_data,
            &image_a[(size_t)addresses[1] * PS1_FRAME_SIZE],
            PS1_FRAME_SIZE);
    pipeline_assert_no_dirty_frames();
}
