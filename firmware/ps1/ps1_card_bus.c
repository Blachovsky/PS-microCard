#include "ps1/ps1_card_bus.h"

#include "board/hardware_config.h"
#include "ps1/ps1_card_bus_internal.h"
#include "ps1/ps1_card_emulator.h"
#include "pico/stdlib.h"

#ifndef UNIT_TEST
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "ps1_card_bus.pio.h"
#endif

#include <stdbool.h>
#include <stdint.h>

#define PS1_MC_STATUS_POWER_ON  0x08u
#define PS1_MC_STATUS_WRITE_ERR 0x04u
#define PS1_MC_ACK_GOOD         0x47u
#define PS1_MC_ACK_ERROR        0x43u
#define PS1_MC_ACK_BAD_SECTOR   0xFFu
#define PS1_CARD_SWAP_ABSENT_US (1500u * 1000u)
#define PS1_CARD_SWAP_MIN_PROBES 2u

#ifndef UNIT_TEST
#define PS1_BUS_PIO_CLOCK_HZ       2500000u
#define PS1_BUS_BYTE_TIMEOUT_US    500u

_Static_assert(PS1_CS_PIN == PS1_CMD_PIN + 1,
               "PIO RX requires CMD and CS on consecutive GPIOs");
_Static_assert(PS1_SCK_PIN == PS1_CMD_PIN + 2,
               "PIO RX requires SCK two GPIOs after CMD");
_Static_assert(PS1_CS_PIN == PS1_DATA_PIN + 2,
               "PIO TX requires CS two GPIOs after DATA");
_Static_assert(PS1_SCK_PIN == PS1_DATA_PIN + 3,
               "PIO TX requires SCK three GPIOs after DATA");
#endif

static volatile uint8_t ps1_mc_status = PS1_MC_STATUS_POWER_ON;
static volatile bool ps1_card_present;
static volatile bool ps1_pause_requested;
static volatile bool ps1_pause_active;
static volatile bool ps1_swap_absent_pending;
static volatile uint32_t ps1_swap_absent_until_us;
static volatile uint32_t ps1_swap_absent_probe_count;

#ifndef UNIT_TEST
static PIO ps1_bus_pio = pio0;
static uint ps1_bus_rx_sm;
static uint ps1_bus_tx_sm;
static uint ps1_bus_rx_offset;
static uint ps1_bus_tx_offset;
static pio_sm_config ps1_bus_rx_config;
static pio_sm_config ps1_bus_tx_config;
static bool ps1_bus_pio_initialized;
#endif

#ifdef UNIT_TEST
static ps1_bus_test_xfer_fn_t ps1_bus_test_xfer_fn;
static bool ps1_bus_test_pause_auto_ack;
#endif

void __not_in_flash_func(ps1_bus_service_pause_if_requested)(void) {
    if (!__atomic_load_n(&ps1_pause_requested, __ATOMIC_ACQUIRE)) {
        return;
    }

    ps1emu_release_lines();
    __atomic_store_n(&ps1_pause_active, true, __ATOMIC_RELEASE);

    while (__atomic_load_n(&ps1_pause_requested, __ATOMIC_ACQUIRE)) {
        tight_loop_contents();
    }

#ifndef UNIT_TEST
    uint32_t deadline = time_us_32() + 5000u;

    while (gpio_get(PS1_CS_PIN) == 0 &&
           (int32_t)(time_us_32() - deadline) < 0) {
        tight_loop_contents();
    }

    ps1_bus_prepare_next_transaction();
#endif

    __atomic_store_n(&ps1_pause_active, false, __ATOMIC_RELEASE);
}

void ps1_bus_request_pause_blocking(void) {
    __atomic_store_n(&ps1_pause_requested, true, __ATOMIC_RELEASE);

#ifdef UNIT_TEST
    if (ps1_bus_test_pause_auto_ack) {
        __atomic_store_n(&ps1_pause_active, true, __ATOMIC_RELEASE);
        return;
    }
#endif

    while (!__atomic_load_n(&ps1_pause_active, __ATOMIC_ACQUIRE)) {
        busy_wait_us_32(50);
    }
}

void ps1_bus_release_pause(void) {
    __atomic_store_n(&ps1_pause_requested, false, __ATOMIC_RELEASE);

#ifdef UNIT_TEST
    if (ps1_bus_test_pause_auto_ack) {
        __atomic_store_n(&ps1_pause_active, false, __ATOMIC_RELEASE);
        return;
    }
#endif

    while (__atomic_load_n(&ps1_pause_active, __ATOMIC_ACQUIRE)) {
        busy_wait_us_32(50);
    }
}

void ps1_bus_set_card_present(bool present) {
    __atomic_store_n(&ps1_card_present, present, __ATOMIC_RELEASE);

    if (!present) {
        __atomic_store_n(&ps1_mc_status,
                         PS1_MC_STATUS_POWER_ON,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&ps1_swap_absent_pending, false, __ATOMIC_RELEASE);
        __atomic_store_n(&ps1_swap_absent_until_us, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&ps1_swap_absent_probe_count, 0u, __ATOMIC_RELEASE);
    }
}

void ps1_bus_begin_card_swap_absent(void) {
    __atomic_store_n(&ps1_mc_status,
                     PS1_MC_STATUS_POWER_ON,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_swap_absent_until_us, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_swap_absent_probe_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_swap_absent_pending, true, __ATOMIC_RELEASE);
}

bool __not_in_flash_func(ps1_bus_should_ignore_transaction_for_swap)(void) {
    if (!__atomic_load_n(&ps1_card_present, __ATOMIC_ACQUIRE)) {
        return true;
    }

    if (!__atomic_load_n(&ps1_swap_absent_pending, __ATOMIC_ACQUIRE)) {
        return false;
    }

    uint32_t now_us = time_us_32();
    uint32_t absent_until_us = __atomic_load_n(&ps1_swap_absent_until_us,
                                               __ATOMIC_ACQUIRE);

    if (absent_until_us == 0u) {
        absent_until_us = now_us + PS1_CARD_SWAP_ABSENT_US;
        __atomic_store_n(&ps1_swap_absent_until_us,
                         absent_until_us,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&ps1_swap_absent_probe_count, 1u, __ATOMIC_RELEASE);
        return true;
    }

    uint32_t probe_count = __atomic_add_fetch(&ps1_swap_absent_probe_count,
                                              1u,
                                              __ATOMIC_ACQ_REL);

    if ((int32_t)(now_us - absent_until_us) < 0 ||
        probe_count <= PS1_CARD_SWAP_MIN_PROBES) {
        return true;
    }

    __atomic_store_n(&ps1_swap_absent_pending, false, __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_swap_absent_until_us, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_swap_absent_probe_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&ps1_mc_status,
                     PS1_MC_STATUS_POWER_ON,
                     __ATOMIC_RELEASE);
    return false;
}

void ps1_bus_init(void) {
#ifndef UNIT_TEST
    int rx_sm = pio_claim_unused_sm(ps1_bus_pio, true);
    int tx_sm = pio_claim_unused_sm(ps1_bus_pio, true);
    int rx_offset = pio_add_program(ps1_bus_pio,
                                    &ps1_card_cmd_rx_program);
    int tx_offset = pio_add_program(ps1_bus_pio,
                                    &ps1_card_data_tx_program);

    ps1_bus_rx_sm = (uint)rx_sm;
    ps1_bus_tx_sm = (uint)tx_sm;
    ps1_bus_rx_offset = (uint)rx_offset;
    ps1_bus_tx_offset = (uint)tx_offset;

    float clock_divider = (float)clock_get_hz(clk_sys) /
                          (float)PS1_BUS_PIO_CLOCK_HZ;

    ps1_bus_rx_config = ps1_card_cmd_rx_program_get_default_config(
            ps1_bus_rx_offset);
    sm_config_set_in_pins(&ps1_bus_rx_config, PS1_CMD_PIN);
    sm_config_set_in_shift(&ps1_bus_rx_config, true, true, 8u);
    sm_config_set_fifo_join(&ps1_bus_rx_config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&ps1_bus_rx_config, clock_divider);

    ps1_bus_tx_config = ps1_card_data_tx_program_get_default_config(
            ps1_bus_tx_offset);
    sm_config_set_in_pins(&ps1_bus_tx_config, PS1_DATA_PIN);
    sm_config_set_out_pins(&ps1_bus_tx_config, PS1_DATA_PIN, 1u);
    sm_config_set_set_pins(&ps1_bus_tx_config, PS1_DATA_PIN, 1u);
    sm_config_set_sideset_pins(&ps1_bus_tx_config, PS1_ACK_PIN);
    sm_config_set_out_shift(&ps1_bus_tx_config, true, true, 8u);
    sm_config_set_fifo_join(&ps1_bus_tx_config, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&ps1_bus_tx_config, clock_divider);

    /* DATA and ACK are open-drain: the output latch is always low. */
    gpio_put(PS1_DATA_PIN, 0);
    gpio_put(PS1_ACK_PIN, 0);
    pio_gpio_init(ps1_bus_pio, PS1_DATA_PIN);
    pio_gpio_init(ps1_bus_pio, PS1_ACK_PIN);

    ps1_bus_pio_initialized = true;
#endif
}

void __not_in_flash_func(ps1_bus_prepare_next_transaction)(void) {
#ifndef UNIT_TEST
    if (!ps1_bus_pio_initialized || gpio_get(PS1_CS_PIN) == 0) {
        return;
    }

    uint32_t sm_mask = (1u << ps1_bus_rx_sm) |
                       (1u << ps1_bus_tx_sm);

    pio_set_sm_mask_enabled(ps1_bus_pio, sm_mask, false);
    pio_sm_init(ps1_bus_pio,
                ps1_bus_rx_sm,
                ps1_bus_rx_offset,
                &ps1_bus_rx_config);
    pio_sm_init(ps1_bus_pio,
                ps1_bus_tx_sm,
                ps1_bus_tx_offset,
                &ps1_bus_tx_config);

    pio_sm_set_consecutive_pindirs(ps1_bus_pio,
                                   ps1_bus_rx_sm,
                                   PS1_CMD_PIN,
                                   1u,
                                   false);
    pio_sm_set_consecutive_pindirs(ps1_bus_pio,
                                   ps1_bus_tx_sm,
                                   PS1_DATA_PIN,
                                   1u,
                                   false);
    pio_sm_set_consecutive_pindirs(ps1_bus_pio,
                                   ps1_bus_tx_sm,
                                   PS1_ACK_PIN,
                                   1u,
                                   false);
    pio_sm_set_pins_with_mask(ps1_bus_pio,
                              ps1_bus_tx_sm,
                              0u,
                              (1u << PS1_DATA_PIN) |
                                      (1u << PS1_ACK_PIN));

    pio_enable_sm_mask_in_sync(ps1_bus_pio, sm_mask);
#endif
}

static void __not_in_flash_func(ps1_data_release)(void) {
    gpio_set_dir(PS1_DATA_PIN, GPIO_IN);
}

static void __not_in_flash_func(ps1_ack_release)(void) {
    gpio_set_dir(PS1_ACK_PIN, GPIO_IN);
}

void __not_in_flash_func(ps1emu_release_lines)(void) {
#ifndef UNIT_TEST
    if (ps1_bus_pio_initialized) {
        uint32_t sm_mask = (1u << ps1_bus_rx_sm) |
                           (1u << ps1_bus_tx_sm);

        pio_set_sm_mask_enabled(ps1_bus_pio, sm_mask, false);
        pio_sm_clear_fifos(ps1_bus_pio, ps1_bus_rx_sm);
        pio_sm_clear_fifos(ps1_bus_pio, ps1_bus_tx_sm);
        pio_sm_set_consecutive_pindirs(ps1_bus_pio,
                                       ps1_bus_tx_sm,
                                       PS1_DATA_PIN,
                                       1u,
                                       false);
        pio_sm_set_consecutive_pindirs(ps1_bus_pio,
                                       ps1_bus_tx_sm,
                                       PS1_ACK_PIN,
                                       1u,
                                       false);
        return;
    }
#endif

    ps1_data_release();
    ps1_ack_release();
}

#ifdef UNIT_TEST
static void __not_in_flash_func(ps1_data_drive_low)(void) {
    gpio_put(PS1_DATA_PIN, 0);
    gpio_set_dir(PS1_DATA_PIN, GPIO_OUT);
}

static void __not_in_flash_func(ps1_data_write_bit)(uint8_t bit) {
    if (bit) {
        ps1_data_release();
    } else {
        ps1_data_drive_low();
    }
}

static void __not_in_flash_func(ps1_ack_drive_low)(void) {
    gpio_put(PS1_ACK_PIN, 0);
    gpio_set_dir(PS1_ACK_PIN, GPIO_OUT);
}

static void __not_in_flash_func(ps1emu_ack_pulse)(void) {
    ps1_ack_drive_low();

    uint32_t start = time_us_32();
    while ((uint32_t)(time_us_32() - start) < PS1_ACK_PULSE_US) {
        tight_loop_contents();
    }

    ps1_ack_release();
}

static ps1_bus_xfer_result_t __not_in_flash_func(wait_sck_level)(
        int level,
        uint32_t timeout_loops) {
    while (gpio_get(PS1_SCK_PIN) != level) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            return PS1_BUS_XFER_ABORTED;
        }

        if (--timeout_loops == 0) {
            return PS1_BUS_XFER_CLOCK_TIMEOUT;
        }

        tight_loop_contents();
    }

    return PS1_BUS_XFER_OK;
}

static uint8_t __not_in_flash_func(ps1emu_recv_send_byte_internal)(
        uint8_t tx,
        bool ack_after,
        ps1_bus_xfer_result_t *result) {
    uint8_t rx = 0;

    if (result != NULL) {
        *result = PS1_BUS_XFER_ABORTED;
    }

    for (int bit = 0; bit < 8; ++bit) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            ps1emu_release_lines();
            return rx;
        }

        ps1_data_write_bit((tx >> bit) & 1u);

        ps1_bus_xfer_result_t wait_result =
                wait_sck_level(0, PS1_BUS_CLOCK_TIMEOUT_LOOPS);
        if (wait_result != PS1_BUS_XFER_OK) {
            if (result != NULL) {
                *result = wait_result;
            }
            ps1emu_release_lines();
            return rx;
        }

        wait_result = wait_sck_level(1, PS1_BUS_CLOCK_TIMEOUT_LOOPS);
        if (wait_result != PS1_BUS_XFER_OK) {
            if (result != NULL) {
                *result = wait_result;
            }
            ps1emu_release_lines();
            return rx;
        }

        if (gpio_get(PS1_CMD_PIN)) {
            rx |= (uint8_t)(1u << bit);
        }
    }

    if (ack_after && gpio_get(PS1_CS_PIN) == 0) {
        ps1emu_ack_pulse();
    }

    if (result != NULL) {
        *result = PS1_BUS_XFER_OK;
    }

    return rx;
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_hardware_xfer)(
        uint8_t tx,
        uint8_t *rx,
        bool ack_after) {
    ps1_bus_xfer_result_t result;
    uint8_t value = ps1emu_recv_send_byte_internal(tx,
                                                   ack_after,
                                                   &result);

    if (rx) {
        *rx = value;
    }

    return result;
}
#else
static ps1_bus_xfer_result_t __not_in_flash_func(ps1_bus_pio_receive_byte)(
        uint8_t *rx) {
    uint32_t deadline = time_us_32() + PS1_BUS_BYTE_TIMEOUT_US;

    while (pio_sm_is_rx_fifo_empty(ps1_bus_pio, ps1_bus_rx_sm)) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            return PS1_BUS_XFER_ABORTED;
        }

        if ((int32_t)(time_us_32() - deadline) >= 0) {
            return PS1_BUS_XFER_CLOCK_TIMEOUT;
        }

        tight_loop_contents();
    }

    uint8_t value = (uint8_t)(pio_sm_get(ps1_bus_pio,
                                         ps1_bus_rx_sm) >> 24);

    if (rx != NULL) {
        *rx = value;
    }

    return PS1_BUS_XFER_OK;
}

/*
 * For the first byte ack_before is false and DATA stays released.  Every
 * following call queues its response first; only then does the TX state
 * machine assert ACK for the byte received by the preceding call.
 */
static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_hardware_xfer)(
        uint8_t tx,
        uint8_t *rx,
        bool ack_before) {
    if (!ps1_bus_pio_initialized) {
        return PS1_BUS_XFER_ABORTED;
    }

    if (ack_before) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            return PS1_BUS_XFER_ABORTED;
        }

        pio_sm_put_blocking(ps1_bus_pio,
                            ps1_bus_tx_sm,
                            (~(uint32_t)tx) & 0xFFu);
    }

    return ps1_bus_pio_receive_byte(rx);
}
#endif

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_xfer)(
        uint8_t tx,
        uint8_t *rx,
        bool ack_before) {
#ifdef UNIT_TEST
    if (ps1_bus_test_xfer_fn != NULL) {
        return ps1_bus_test_xfer_fn(tx, rx, ack_before);
    }
#endif

    return ps1emu_hardware_xfer(tx, rx, ack_before);
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_exchange_byte)(
        uint8_t tx,
        uint8_t *rx) {
    return ps1emu_xfer(tx, rx, true);
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_send_byte)(
        uint8_t tx) {
    return ps1emu_exchange_byte(tx, NULL);
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_send_dummy_bytes)(
        uint8_t value,
        int count) {
    for (int i = 0; i < count; ++i) {
        ps1_bus_xfer_result_t result = ps1emu_send_byte(value);

        if (result != PS1_BUS_XFER_OK) {
            return result;
        }
    }

    return PS1_BUS_XFER_OK;
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_handle_read)(void) {
    ps1_bus_xfer_result_t result = ps1emu_send_byte(0x5A);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    result = ps1emu_send_byte(0x5D);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    uint8_t addr_msb;
    uint8_t addr_lsb;

    result = ps1emu_exchange_byte(0x00, &addr_msb);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    result = ps1emu_exchange_byte(0x00, &addr_lsb);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    uint16_t frame_addr = ((uint16_t)addr_msb << 8) | addr_lsb;
    uint8_t *frame = get_frame_ptr(frame_addr);

    result = ps1emu_send_byte(0x5C);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    result = ps1emu_send_byte(0x5D);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    result = ps1emu_send_byte(addr_msb);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    result = ps1emu_send_byte(addr_lsb);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    if (frame == NULL) {
        result = ps1emu_send_dummy_bytes(0xFF, PS1_FRAME_SIZE);
        if (result != PS1_BUS_XFER_OK) {
            return result;
        }

        result = ps1emu_send_byte(0xFF);
        if (result != PS1_BUS_XFER_OK) {
            return result;
        }

        return ps1emu_send_byte(PS1_MC_ACK_BAD_SECTOR);
    }

    uint8_t checksum = ps1_frame_checksum(frame_addr, frame);

    for (int i = 0; i < PS1_FRAME_SIZE; ++i) {
        result = ps1emu_send_byte(frame[i]);
        if (result != PS1_BUS_XFER_OK) {
            return result;
        }
    }

    result = ps1emu_send_byte(checksum);
    if (result != PS1_BUS_XFER_OK) {
        return result;
    }

    return ps1emu_send_byte(PS1_MC_ACK_GOOD);
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_handle_write)(void) {
    uint8_t data[PS1_FRAME_SIZE];
    ps1_bus_xfer_result_t xfer_result = ps1emu_send_byte(0x5A);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    xfer_result = ps1emu_send_byte(0x5D);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    uint8_t addr_msb;
    uint8_t addr_lsb;

    xfer_result = ps1emu_exchange_byte(0x00, &addr_msb);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    xfer_result = ps1emu_exchange_byte(0x00, &addr_lsb);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    uint16_t frame_addr = ((uint16_t)addr_msb << 8) | addr_lsb;

    for (int i = 0; i < PS1_FRAME_SIZE; ++i) {
        xfer_result = ps1emu_exchange_byte(0x00, &data[i]);
        if (xfer_result != PS1_BUS_XFER_OK) {
            return xfer_result;
        }
    }

    uint8_t received_checksum;

    xfer_result = ps1emu_exchange_byte(0x00, &received_checksum);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    uint8_t calculated_checksum = ps1_frame_checksum(frame_addr, data);
    uint8_t result = PS1_MC_ACK_GOOD;

    if (get_frame_ptr(frame_addr) == NULL) {
        ps1_mc_status |= PS1_MC_STATUS_WRITE_ERR;
        result = PS1_MC_ACK_BAD_SECTOR;
    } else if (received_checksum != calculated_checksum) {
        ps1_mc_status |= PS1_MC_STATUS_WRITE_ERR;
        result = PS1_MC_ACK_ERROR;
    } else if (ps1emu_commit_frame(frame_addr, data) != PS1EMU_RESULT_OK) {
        ps1_mc_status |= PS1_MC_STATUS_WRITE_ERR;
        result = PS1_MC_ACK_ERROR;
    } else {
        ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_POWER_ON;
        ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_WRITE_ERR;
    }

    xfer_result = ps1emu_send_byte(0x5C);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    xfer_result = ps1emu_send_byte(0x5D);
    if (xfer_result != PS1_BUS_XFER_OK) {
        return xfer_result;
    }

    return ps1emu_send_byte(result);
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_handle_status)(void) {
    static const uint8_t response[] = {
            0x5A, 0x5D, 0x5C, 0x5D, 0x04, 0x00, 0x00, 0x80,
    };

    for (size_t i = 0; i < sizeof(response); ++i) {
        ps1_bus_xfer_result_t result = ps1emu_send_byte(response[i]);

        if (result != PS1_BUS_XFER_OK) {
            return result;
        }
    }

    return PS1_BUS_XFER_OK;
}

void __not_in_flash_func(ps1emu_handle_transaction)(void) {
    ps1_bus_xfer_result_t result;
    uint8_t access;

    result = ps1emu_xfer(0xFF, &access, false);

    if (result != PS1_BUS_XFER_OK || access != 0x81) {
        ps1emu_release_lines();
        return;
    }

    uint8_t status_snapshot = ps1_mc_status;
    uint8_t command;

    result = ps1emu_xfer(status_snapshot, &command, true);

    ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_WRITE_ERR;

    if (result != PS1_BUS_XFER_OK) {
        ps1emu_release_lines();
        return;
    }

    if (command == 0x52) {
        (void)ps1emu_handle_read();
    } else if (command == 0x57) {
        (void)ps1emu_handle_write();
    } else if (command == 0x53) {
        (void)ps1emu_handle_status();
    }

    ps1emu_release_lines();
}

#ifdef UNIT_TEST
void ps1_bus_test_reset_state(void) {
    ps1_mc_status = PS1_MC_STATUS_POWER_ON;
    ps1_card_present = false;
    ps1_pause_requested = false;
    ps1_pause_active = false;
    ps1_swap_absent_pending = false;
    ps1_swap_absent_until_us = 0u;
    ps1_swap_absent_probe_count = 0u;
    ps1_bus_test_xfer_fn = NULL;
    ps1_bus_test_pause_auto_ack = false;
}

void ps1_bus_test_set_transport(ps1_bus_test_xfer_fn_t xfer_fn) {
    ps1_bus_test_xfer_fn = xfer_fn;
}

void ps1_bus_test_set_pause_auto_ack(bool enabled) {
    ps1_bus_test_pause_auto_ack = enabled;
}

ps1_bus_xfer_result_t ps1_bus_test_hardware_xfer(uint8_t tx,
                                                uint8_t *rx,
                                                bool ack_after) {
    return ps1emu_hardware_xfer(tx, rx, ack_after);
}
#endif
