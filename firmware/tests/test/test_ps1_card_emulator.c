#include "unity.h"

#include "ps1/ps1_card_emulator.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {
    memset(card_image, 0, sizeof(card_image));
    ps1emu_storage_state_init();
}

void tearDown(void) {
}

static void fill_frame_pattern(uint8_t frame[PS1_FRAME_SIZE], uint8_t seed) {
    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        frame[i] = (uint8_t)(seed + (uint8_t)i);
    }
}

void test_get_frame_ptr_returns_frame_start_for_valid_address(void) {
    TEST_ASSERT_EQUAL_PTR(&card_image[0], get_frame_ptr(0u));
    TEST_ASSERT_EQUAL_PTR(&card_image[PS1_CARD_SIZE - PS1_FRAME_SIZE],
                          get_frame_ptr(PS1_FRAME_COUNT - 1u));
}

void test_get_frame_ptr_rejects_address_outside_card(void) {
    TEST_ASSERT_NULL(get_frame_ptr(PS1_FRAME_COUNT));
    TEST_ASSERT_NULL(get_frame_ptr(UINT16_MAX));
}

void test_frame_checksum_combines_address_bytes_and_frame_data(void) {
    uint8_t frame[PS1_FRAME_SIZE] = {0};

    frame[0] = 0x12u;
    frame[PS1_FRAME_SIZE - 1u] = 0xA5u;

    TEST_ASSERT_EQUAL_HEX8(0xB4u, ps1_frame_checksum(0x0102u, frame));
}

void test_commit_frame_rejects_invalid_arguments(void) {
    uint8_t frame[PS1_FRAME_SIZE] = {0};

    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_INVALID_ARGUMENT,
                          ps1emu_commit_frame(0u, NULL));
    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_FRAME_OUT_OF_RANGE,
                          ps1emu_commit_frame(PS1_FRAME_COUNT, frame));
    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_FRAME_OUT_OF_RANGE,
                          ps1emu_commit_frame(UINT16_MAX, frame));
}

void test_take_changed_frame_rejects_invalid_arguments(void) {
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;
    uint8_t frame[PS1_FRAME_SIZE] = {0};

    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_INVALID_ARGUMENT,
                          ps1emu_take_changed_frame(NULL,
                                                    &frame_version,
                                                    frame));
    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_INVALID_ARGUMENT,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    NULL,
                                                    frame));
    TEST_ASSERT_EQUAL_INT(PS1EMU_ERROR_INVALID_ARGUMENT,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    NULL));
}

void test_commit_and_take_changed_frame_returns_stable_copy(void) {
    uint8_t committed[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    fill_frame_pattern(committed, 0x5Au);

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(17u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(17u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(2u, frame_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(committed, snapshot, PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
}

void test_commits_to_first_middle_and_last_frames_are_isolated_and_pending(void) {
    static uint8_t expected_card[PS1_CARD_SIZE];
    const uint16_t addresses[] = {0u, 10u, 100u, PS1_FRAME_COUNT - 1u};
    uint8_t committed[4][PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    memset(expected_card, 0, sizeof(expected_card));

    for (size_t i = 0; i < 4u; ++i) {
        fill_frame_pattern(committed[i], (uint8_t)(0x10u + i * 0x20u));
        memcpy(&expected_card[(size_t)addresses[i] * PS1_FRAME_SIZE],
               committed[i],
               PS1_FRAME_SIZE);

        TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                              ps1emu_commit_frame(addresses[i], committed[i]));
    }

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card, card_image, PS1_CARD_SIZE);

    for (size_t i = 0; i < 4u; ++i) {
        TEST_ASSERT_EQUAL_INT(
                PS1EMU_RESULT_OK,
                ps1emu_take_changed_frame(&frame_addr,
                                          &frame_version,
                                          snapshot));
        TEST_ASSERT_EQUAL_UINT16(addresses[i], frame_addr);
        TEST_ASSERT_EQUAL_UINT32(2u, frame_version);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(committed[i],
                                      snapshot,
                                      PS1_FRAME_SIZE);
    }

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
}

void test_two_identical_commits_still_publish_latest_version(void) {
    uint8_t committed[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    fill_frame_pattern(committed, 0xA0u);

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(23u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(23u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(23u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(4u, frame_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(committed, snapshot, PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
}

void test_version_counter_wraps_through_uint32_max_to_zero(void) {
    uint8_t committed[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = UINT32_MAX;

    fill_frame_pattern(committed, 0xC0u);
    ps1emu_test_seed_frame_version(31u, UINT32_MAX - 1u);

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(31u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(31u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(0u, frame_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(committed, snapshot, PS1_FRAME_SIZE);

    ps1emu_rollback_unconfirmed_frames();

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(31u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(0u, frame_version);

    ps1emu_confirm_frame_synced(frame_addr, frame_version);
    ps1emu_rollback_unconfirmed_frames();

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
}

void test_second_commit_before_take_returns_only_latest_frame_version(void) {
    uint8_t first[PS1_FRAME_SIZE];
    uint8_t second[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    memset(first, 0x11, sizeof(first));
    memset(second, 0x22, sizeof(second));

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(3u, first));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(3u, second));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(3u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(4u, frame_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second, snapshot, PS1_FRAME_SIZE);
}

void test_rollback_requeues_frame_that_was_not_confirmed(void) {
    uint8_t committed[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    memset(committed, 0x33, sizeof(committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(9u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));

    ps1emu_rollback_unconfirmed_frames();

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(9u, frame_addr);
    TEST_ASSERT_EQUAL_UINT32(2u, frame_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(committed, snapshot, PS1_FRAME_SIZE);
}

void test_rollback_does_not_requeue_confirmed_frame(void) {
    uint8_t committed[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t frame_addr = 0u;
    uint32_t frame_version = 0u;

    memset(committed, 0x44, sizeof(committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_commit_frame(11u, committed));
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));

    ps1emu_confirm_frame_synced(frame_addr, frame_version);
    ps1emu_rollback_unconfirmed_frames();

    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&frame_addr,
                                                    &frame_version,
                                                    snapshot));
}
