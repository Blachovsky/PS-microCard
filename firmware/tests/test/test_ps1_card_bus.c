#include "unity.h"

#include "hardware_config.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_bus_internal.h"
#include "ps1/ps1_card_emulator.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SCRIPT_CAPACITY = 160,
    ACCESS_INDEX = 0,
    COMMAND_INDEX = 1,
    READ_ADDR_MSB_INDEX = 4,
    READ_ADDR_LSB_INDEX = 5,
    READ_DATA_INDEX = 10,
    READ_CHECKSUM_INDEX = READ_DATA_INDEX + PS1_FRAME_SIZE,
    READ_RESULT_INDEX = READ_CHECKSUM_INDEX + 1,
    READ_TRANSFER_COUNT = READ_RESULT_INDEX + 1,
    WRITE_ADDR_MSB_INDEX = 4,
    WRITE_ADDR_LSB_INDEX = 5,
    WRITE_DATA_INDEX = 6,
    WRITE_CHECKSUM_INDEX = WRITE_DATA_INDEX + PS1_FRAME_SIZE,
    WRITE_TRAILER_INDEX = WRITE_CHECKSUM_INDEX + 1,
    WRITE_RESULT_INDEX = WRITE_TRAILER_INDEX + 2,
    WRITE_TRANSFER_COUNT = WRITE_RESULT_INDEX + 1,
    STATUS_RESPONSE_INDEX = 2,
    STATUS_TRANSFER_COUNT = STATUS_RESPONSE_INDEX + 8,
};

typedef struct {
    uint8_t rx[SCRIPT_CAPACITY];
    uint8_t tx[SCRIPT_CAPACITY];
    ps1_bus_xfer_result_t result[SCRIPT_CAPACITY];
    bool send_ack[SCRIPT_CAPACITY];
    size_t length;
    size_t position;
    bool overrun;
} xfer_script_t;

typedef struct {
    int32_t cs_high_after_read;
    uint32_t cs_read_count;
    uint8_t cmd_byte;
    uint32_t cmd_bit_index;
    int sck_target;
    uint32_t sck_delay_per_edge;
    uint32_t sck_delay_remaining;
    bool sck_never_edges;
    uint32_t now_us;
    uint32_t tight_loop_count;
    uint32_t ack_drive_count;
    uint32_t ack_release_count;
    uint32_t data_release_count;
} fake_gpio_t;

static xfer_script_t script;
static fake_gpio_t fake_gpio;
static uint32_t manual_ack_count;
static uint8_t expected_card[PS1_CARD_SIZE];

static void reset_fake_gpio(void) {
    memset(&fake_gpio, 0, sizeof(fake_gpio));
    fake_gpio.cs_high_after_read = -1;
}

static void prepare_script(size_t length) {
    memset(&script, 0, sizeof(script));
    TEST_ASSERT_TRUE(length <= SCRIPT_CAPACITY);
    script.length = length;
}

static ps1_bus_xfer_result_t scripted_xfer(uint8_t tx,
                                           uint8_t *rx,
                                           bool send_ack) {
    if (script.position >= script.length) {
        script.overrun = true;
        return PS1_BUS_XFER_ABORTED;
    }

    size_t index = script.position++;
    script.tx[index] = tx;
    script.send_ack[index] = send_ack;

    if (rx != NULL) {
        *rx = script.rx[index];
    }

    return script.result[index];
}

static void record_manual_ack(void) {
    ++manual_ack_count;
}

static void reset_fixture(void) {
    memset(card_image, 0, sizeof(card_image));
    memset(expected_card, 0, sizeof(expected_card));
    ps1emu_storage_state_init();
    ps1_bus_test_reset_state();
    reset_fake_gpio();
    prepare_script(0u);
    manual_ack_count = 0u;
    ps1_bus_test_set_transport(scripted_xfer, record_manual_ack);
}

void setUp(void) {
    reset_fixture();
}

void tearDown(void) {
}

int gpio_get(uint gpio) {
    if (gpio == PS1_CS_PIN) {
        bool high = fake_gpio.cs_high_after_read >= 0 &&
                    fake_gpio.cs_read_count >=
                            (uint32_t)fake_gpio.cs_high_after_read;
        ++fake_gpio.cs_read_count;
        return high ? 1 : 0;
    }

    if (gpio == PS1_SCK_PIN) {
        if (fake_gpio.sck_never_edges) {
            return fake_gpio.sck_target ? 0 : 1;
        }

        if (fake_gpio.sck_delay_remaining > 0u) {
            --fake_gpio.sck_delay_remaining;
            return fake_gpio.sck_target ? 0 : 1;
        }

        int result = fake_gpio.sck_target;
        fake_gpio.sck_target ^= 1;
        fake_gpio.sck_delay_remaining = fake_gpio.sck_delay_per_edge;
        return result;
    }

    if (gpio == PS1_CMD_PIN) {
        int bit = (fake_gpio.cmd_byte >> fake_gpio.cmd_bit_index) & 1u;
        ++fake_gpio.cmd_bit_index;
        return bit;
    }

    return 0;
}

void gpio_put(uint gpio, bool value) {
    if (gpio == PS1_ACK_PIN && !value) {
        ++fake_gpio.ack_drive_count;
    }
}

void gpio_set_dir(uint gpio, bool out) {
    if (gpio == PS1_ACK_PIN) {
        if (out == GPIO_OUT) {
            ++fake_gpio.ack_drive_count;
        } else {
            ++fake_gpio.ack_release_count;
        }
    } else if (gpio == PS1_DATA_PIN && out == GPIO_IN) {
        ++fake_gpio.data_release_count;
    }
}

uint32_t time_us_32(void) {
    return fake_gpio.now_us++;
}

void tight_loop_contents(void) {
    ++fake_gpio.tight_loop_count;
}

void busy_wait_us_32(uint32_t delay_us) {
    fake_gpio.now_us += delay_us;
}

static void fill_frame_pattern(uint8_t frame[PS1_FRAME_SIZE], uint8_t seed) {
    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        frame[i] = (uint8_t)(seed + (uint8_t)(i * 3u));
    }
}

static uint8_t expected_xor(uint16_t frame_addr,
                            const uint8_t data[PS1_FRAME_SIZE]) {
    uint8_t checksum = (uint8_t)(frame_addr & 0xFFu) ^
                       (uint8_t)(frame_addr >> 8);

    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        checksum ^= data[i];
    }

    return checksum;
}

static void prepare_read(uint16_t frame_addr) {
    prepare_script(READ_TRANSFER_COUNT);
    script.rx[ACCESS_INDEX] = 0x81u;
    script.rx[COMMAND_INDEX] = 0x52u;
    script.rx[READ_ADDR_MSB_INDEX] = (uint8_t)(frame_addr >> 8);
    script.rx[READ_ADDR_LSB_INDEX] = (uint8_t)(frame_addr & 0xFFu);
}

static void prepare_write(uint16_t frame_addr,
                          const uint8_t data[PS1_FRAME_SIZE]) {
    prepare_script(WRITE_TRANSFER_COUNT);
    script.rx[ACCESS_INDEX] = 0x81u;
    script.rx[COMMAND_INDEX] = 0x57u;
    script.rx[WRITE_ADDR_MSB_INDEX] = (uint8_t)(frame_addr >> 8);
    script.rx[WRITE_ADDR_LSB_INDEX] = (uint8_t)(frame_addr & 0xFFu);
    memcpy(&script.rx[WRITE_DATA_INDEX], data, PS1_FRAME_SIZE);
    script.rx[WRITE_CHECKSUM_INDEX] = expected_xor(frame_addr, data);
}

static void prepare_status(void) {
    prepare_script(STATUS_TRANSFER_COUNT);
    script.rx[ACCESS_INDEX] = 0x81u;
    script.rx[COMMAND_INDEX] = 0x53u;
}

static void assert_successful_access_and_ack(size_t transfer_count) {
    TEST_ASSERT_FALSE(script.overrun);
    TEST_ASSERT_EQUAL_UINT32(transfer_count, script.position);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[ACCESS_INDEX]);
    TEST_ASSERT_FALSE(script.send_ack[ACCESS_INDEX]);
    TEST_ASSERT_EQUAL_UINT32(1u, manual_ack_count);

    for (size_t i = 1u; i < transfer_count; ++i) {
        TEST_ASSERT_TRUE(script.send_ack[i]);
    }
}

static void assert_lines_released(void) {
    TEST_ASSERT_TRUE(fake_gpio.data_release_count >= 1u);
    TEST_ASSERT_TRUE(fake_gpio.ack_release_count >= 1u);
}

static void assert_card_contains_only_frame(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]) {
    memset(expected_card, 0, sizeof(expected_card));
    memcpy(&expected_card[(size_t)frame_addr * PS1_FRAME_SIZE],
           data,
           PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card, card_image, PS1_CARD_SIZE);
}

void test_read_0x52_returns_128_bytes_xor_checksum_and_good_result(void) {
    const uint16_t frame_addr = 37u;
    uint8_t expected[PS1_FRAME_SIZE];
    uint8_t *frame = get_frame_ptr(frame_addr);

    TEST_ASSERT_NOT_NULL(frame);
    fill_frame_pattern(expected, 0x21u);
    memcpy(frame, expected, sizeof(expected));
    prepare_read(frame_addr);

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(READ_TRANSFER_COUNT);
    TEST_ASSERT_EQUAL_HEX8(0x08u, script.tx[COMMAND_INDEX]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, script.tx[2]);
    TEST_ASSERT_EQUAL_HEX8(0x5Du, script.tx[3]);
    TEST_ASSERT_EQUAL_HEX8(0x5Cu, script.tx[6]);
    TEST_ASSERT_EQUAL_HEX8(0x5Du, script.tx[7]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(frame_addr >> 8), script.tx[8]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)frame_addr, script.tx[9]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,
                                  &script.tx[READ_DATA_INDEX],
                                  PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_HEX8(expected_xor(frame_addr, expected),
                           script.tx[READ_CHECKSUM_INDEX]);
    TEST_ASSERT_EQUAL_HEX8(0x47u, script.tx[READ_RESULT_INDEX]);
    assert_lines_released();
}

void test_status_0x53_returns_complete_status_response(void) {
    static const uint8_t expected[] = {
            0x5A, 0x5D, 0x5C, 0x5D, 0x04, 0x00, 0x00, 0x80,
    };

    prepare_status();

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(STATUS_TRANSFER_COUNT);
    TEST_ASSERT_EQUAL_HEX8(0x08u, script.tx[COMMAND_INDEX]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,
                                  &script.tx[STATUS_RESPONSE_INDEX],
                                  sizeof(expected));
    assert_lines_released();
}

void test_write_0x57_commits_exactly_128_bytes_and_returns_good_result(void) {
    const uint16_t frame_addr = 48u;
    uint8_t data[PS1_FRAME_SIZE];
    uint8_t snapshot[PS1_FRAME_SIZE] = {0};
    uint16_t changed_addr = 0u;
    uint32_t changed_version = 0u;

    fill_frame_pattern(data, 0x42u);
    prepare_write(frame_addr, data);

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(WRITE_TRANSFER_COUNT);
    assert_card_contains_only_frame(frame_addr, data);
    TEST_ASSERT_EQUAL_HEX8(0x5Cu, script.tx[WRITE_TRAILER_INDEX]);
    TEST_ASSERT_EQUAL_HEX8(0x5Du, script.tx[WRITE_TRAILER_INDEX + 1]);
    TEST_ASSERT_EQUAL_HEX8(0x47u, script.tx[WRITE_RESULT_INDEX]);
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_OK,
                          ps1emu_take_changed_frame(&changed_addr,
                                                    &changed_version,
                                                    snapshot));
    TEST_ASSERT_EQUAL_UINT16(frame_addr, changed_addr);
    TEST_ASSERT_EQUAL_UINT32(2u, changed_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, snapshot, PS1_FRAME_SIZE);
    TEST_ASSERT_EQUAL_INT(PS1EMU_RESULT_NO_CHANGED_FRAME,
                          ps1emu_take_changed_frame(&changed_addr,
                                                    &changed_version,
                                                    snapshot));
    assert_lines_released();
}

void test_read_accepts_first_and_last_frame_addresses(void) {
    const uint16_t addresses[] = {0u, PS1_FRAME_COUNT - 1u};
    uint8_t expected[PS1_FRAME_SIZE];

    for (size_t i = 0; i < 2u; ++i) {
        reset_fixture();
        fill_frame_pattern(expected, (uint8_t)(0x10u + i));
        memcpy(get_frame_ptr(addresses[i]), expected, PS1_FRAME_SIZE);
        prepare_read(addresses[i]);

        ps1emu_handle_transaction();

        assert_successful_access_and_ack(READ_TRANSFER_COUNT);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected,
                                      &script.tx[READ_DATA_INDEX],
                                      PS1_FRAME_SIZE);
        TEST_ASSERT_EQUAL_HEX8(0x47u, script.tx[READ_RESULT_INDEX]);
    }
}

void test_write_accepts_first_and_last_frame_addresses(void) {
    const uint16_t addresses[] = {0u, PS1_FRAME_COUNT - 1u};
    uint8_t data[PS1_FRAME_SIZE];

    for (size_t i = 0; i < 2u; ++i) {
        reset_fixture();
        fill_frame_pattern(data, (uint8_t)(0x30u + i));
        prepare_write(addresses[i], data);

        ps1emu_handle_transaction();

        assert_successful_access_and_ack(WRITE_TRANSFER_COUNT);
        assert_card_contains_only_frame(addresses[i], data);
        TEST_ASSERT_EQUAL_HEX8(0x47u, script.tx[WRITE_RESULT_INDEX]);
    }
}

void test_out_of_range_read_returns_ff_data_checksum_and_bad_sector(void) {
    prepare_read(PS1_FRAME_COUNT);

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(READ_TRANSFER_COUNT);
    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[READ_DATA_INDEX + i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[READ_CHECKSUM_INDEX]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[READ_RESULT_INDEX]);
}

void test_out_of_range_write_returns_bad_sector_without_changing_ram(void) {
    uint8_t data[PS1_FRAME_SIZE];

    fill_frame_pattern(data, 0x62u);
    prepare_write(PS1_FRAME_COUNT, data);

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(WRITE_TRANSFER_COUNT);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card, card_image, PS1_CARD_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[WRITE_RESULT_INDEX]);
}

void test_write_with_bad_checksum_returns_error_without_changing_ram(void) {
    uint8_t data[PS1_FRAME_SIZE];

    fill_frame_pattern(data, 0x72u);
    prepare_write(12u, data);
    script.rx[WRITE_CHECKSUM_INDEX] ^= 0x01u;

    ps1emu_handle_transaction();

    assert_successful_access_and_ack(WRITE_TRANSFER_COUNT);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card, card_image, PS1_CARD_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0x43u, script.tx[WRITE_RESULT_INDEX]);
}

void test_unknown_commands_0x00_and_0xff_are_ignored(void) {
    const uint8_t commands[] = {0x00u, 0xFFu};

    for (size_t i = 0; i < sizeof(commands); ++i) {
        reset_fixture();
        prepare_script(2u);
        script.rx[ACCESS_INDEX] = 0x81u;
        script.rx[COMMAND_INDEX] = commands[i];

        ps1emu_handle_transaction();

        assert_successful_access_and_ack(2u);
        assert_lines_released();
    }
}

void test_access_byte_other_than_0x81_is_rejected_without_ack(void) {
    prepare_script(1u);
    script.rx[ACCESS_INDEX] = 0x80u;

    ps1emu_handle_transaction();

    TEST_ASSERT_EQUAL_UINT32(1u, script.position);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, script.tx[ACCESS_INDEX]);
    TEST_ASSERT_FALSE(script.send_ack[ACCESS_INDEX]);
    TEST_ASSERT_EQUAL_UINT32(0u, manual_ack_count);
    assert_lines_released();
}

void test_cs_abort_at_every_write_byte_stops_at_that_byte(void) {
    uint8_t data[PS1_FRAME_SIZE];

    fill_frame_pattern(data, 0x82u);

    for (size_t abort_index = 0u;
         abort_index < WRITE_TRANSFER_COUNT;
         ++abort_index) {
        reset_fixture();
        prepare_write(77u, data);
        script.result[abort_index] = PS1_BUS_XFER_ABORTED;

        ps1emu_handle_transaction();

        TEST_ASSERT_FALSE(script.overrun);
        TEST_ASSERT_EQUAL_UINT32(abort_index + 1u, script.position);
        assert_lines_released();

        if (abort_index <= WRITE_CHECKSUM_INDEX) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card,
                                          card_image,
                                          PS1_CARD_SIZE);
        } else {
            assert_card_contains_only_frame(77u, data);
        }
    }
}

void test_cs_abort_at_every_read_and_status_byte_stops_at_that_byte(void) {
    uint8_t data[PS1_FRAME_SIZE];

    fill_frame_pattern(data, 0x87u);

    for (size_t abort_index = 0u;
         abort_index < READ_TRANSFER_COUNT;
         ++abort_index) {
        reset_fixture();
        memcpy(get_frame_ptr(91u), data, PS1_FRAME_SIZE);
        prepare_read(91u);
        script.result[abort_index] = PS1_BUS_XFER_ABORTED;

        ps1emu_handle_transaction();

        TEST_ASSERT_FALSE(script.overrun);
        TEST_ASSERT_EQUAL_UINT32(abort_index + 1u, script.position);
        assert_lines_released();
    }

    for (size_t abort_index = 0u;
         abort_index < STATUS_TRANSFER_COUNT;
         ++abort_index) {
        reset_fixture();
        prepare_status();
        script.result[abort_index] = PS1_BUS_XFER_ABORTED;

        ps1emu_handle_transaction();

        TEST_ASSERT_FALSE(script.overrun);
        TEST_ASSERT_EQUAL_UINT32(abort_index + 1u, script.position);
        assert_lines_released();
    }
}

void test_clock_timeout_during_write_data_prevents_commit(void) {
    uint8_t data[PS1_FRAME_SIZE];
    const size_t middle_data_index = WRITE_DATA_INDEX + PS1_FRAME_SIZE / 2u;

    fill_frame_pattern(data, 0x92u);
    prepare_write(88u, data);
    script.result[middle_data_index] = PS1_BUS_XFER_CLOCK_TIMEOUT;

    ps1emu_handle_transaction();

    TEST_ASSERT_EQUAL_UINT32(middle_data_index + 1u, script.position);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_card, card_image, PS1_CARD_SIZE);
    assert_lines_released();
}

void test_hardware_xfer_generates_ack_pulse_and_transfers_lsb_first(void) {
    uint8_t rx = 0u;

    ps1_bus_test_set_transport(NULL, NULL);
    reset_fake_gpio();
    fake_gpio.cmd_byte = 0x3Cu;

    TEST_ASSERT_EQUAL_INT(PS1_BUS_XFER_OK,
                          ps1_bus_test_hardware_xfer(0xA5u, &rx, true));
    TEST_ASSERT_EQUAL_HEX8(0x3Cu, rx);
    TEST_ASSERT_EQUAL_UINT32(2u, fake_gpio.ack_drive_count);
    TEST_ASSERT_EQUAL_UINT32(1u, fake_gpio.ack_release_count);
    TEST_ASSERT_TRUE(fake_gpio.now_us >= PS1_ACK_PULSE_US);
}

void test_hardware_xfer_aborts_when_cs_rises_at_each_bit(void) {
    for (int32_t bit = 0; bit < 8; ++bit) {
        ps1_bus_test_set_transport(NULL, NULL);
        reset_fake_gpio();
        fake_gpio.cs_high_after_read = bit;

        TEST_ASSERT_EQUAL_INT(
                PS1_BUS_XFER_ABORTED,
                ps1_bus_test_hardware_xfer(0x00u, NULL, false));
        assert_lines_released();
    }
}

void test_hardware_xfer_aborts_if_cs_rises_while_waiting_for_clock(void) {
    ps1_bus_test_set_transport(NULL, NULL);
    reset_fake_gpio();
    fake_gpio.sck_never_edges = true;
    fake_gpio.cs_high_after_read = 4;

    TEST_ASSERT_EQUAL_INT(
            PS1_BUS_XFER_ABORTED,
            ps1_bus_test_hardware_xfer(0x00u, NULL, false));
    TEST_ASSERT_TRUE(fake_gpio.tight_loop_count <
                     PS1_BUS_CLOCK_TIMEOUT_LOOPS);
    assert_lines_released();
}

void test_hardware_xfer_times_out_when_no_clock_edge_arrives(void) {
    ps1_bus_test_set_transport(NULL, NULL);
    reset_fake_gpio();
    fake_gpio.sck_never_edges = true;

    TEST_ASSERT_EQUAL_INT(
            PS1_BUS_XFER_CLOCK_TIMEOUT,
            ps1_bus_test_hardware_xfer(0x00u, NULL, false));
    TEST_ASSERT_EQUAL_UINT32(PS1_BUS_CLOCK_TIMEOUT_LOOPS - 1u,
                             fake_gpio.tight_loop_count);
    assert_lines_released();
}

void test_hardware_xfer_accepts_very_slow_edges_before_timeout(void) {
    uint8_t rx = 0u;

    ps1_bus_test_set_transport(NULL, NULL);
    reset_fake_gpio();
    fake_gpio.cmd_byte = 0xC3u;
    fake_gpio.sck_delay_per_edge = PS1_BUS_CLOCK_TIMEOUT_LOOPS - 1u;
    fake_gpio.sck_delay_remaining = PS1_BUS_CLOCK_TIMEOUT_LOOPS - 1u;

    TEST_ASSERT_EQUAL_INT(
            PS1_BUS_XFER_OK,
            ps1_bus_test_hardware_xfer(0xFFu, &rx, false));
    TEST_ASSERT_EQUAL_HEX8(0xC3u, rx);
}

void test_hardware_xfer_rejects_edge_at_exact_timeout_boundary(void) {
    ps1_bus_test_set_transport(NULL, NULL);
    reset_fake_gpio();
    fake_gpio.sck_delay_per_edge = PS1_BUS_CLOCK_TIMEOUT_LOOPS;
    fake_gpio.sck_delay_remaining = PS1_BUS_CLOCK_TIMEOUT_LOOPS;

    TEST_ASSERT_EQUAL_INT(
            PS1_BUS_XFER_CLOCK_TIMEOUT,
            ps1_bus_test_hardware_xfer(0x00u, NULL, false));
    TEST_ASSERT_EQUAL_UINT32(PS1_BUS_CLOCK_TIMEOUT_LOOPS - 1u,
                             fake_gpio.tight_loop_count);
}
