#include "ps1/ps1_card_bus.h"

#include "board/hardware_config.h"
#include "ps1/ps1_card_emulator.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>

#define PS1_MC_STATUS_POWER_ON  0x08u
#define PS1_MC_STATUS_WRITE_ERR 0x04u
#define PS1_MC_ACK_GOOD         0x47u
#define PS1_MC_ACK_ERROR        0x43u
#define PS1_MC_ACK_BAD_SECTOR   0xFFu
#define PS1_CARD_SWAP_ABSENT_US (1500u * 1000u)
#define PS1_CARD_SWAP_MIN_PROBES 2u

typedef enum {
    PS1_BUS_XFER_OK = 0,
    PS1_BUS_XFER_ABORTED,
    PS1_BUS_XFER_CLOCK_TIMEOUT,
} ps1_bus_xfer_result_t;

static volatile uint8_t ps1_mc_status = PS1_MC_STATUS_POWER_ON;
static volatile bool ps1_card_present;
static volatile bool ps1_pause_requested;
static volatile bool ps1_pause_active;
static volatile bool ps1_swap_absent_pending;
static volatile uint32_t ps1_swap_absent_until_us;
static volatile uint32_t ps1_swap_absent_probe_count;

void __not_in_flash_func(ps1_bus_service_pause_if_requested)(void) {
    if (!__atomic_load_n(&ps1_pause_requested, __ATOMIC_ACQUIRE)) {
        return;
    }

    ps1emu_release_lines();
    __atomic_store_n(&ps1_pause_active, true, __ATOMIC_RELEASE);

    while (__atomic_load_n(&ps1_pause_requested, __ATOMIC_ACQUIRE)) {
        tight_loop_contents();
    }

    __atomic_store_n(&ps1_pause_active, false, __ATOMIC_RELEASE);
}

void ps1_bus_request_pause_blocking(void) {
    __atomic_store_n(&ps1_pause_requested, true, __ATOMIC_RELEASE);

    while (!__atomic_load_n(&ps1_pause_active, __ATOMIC_ACQUIRE)) {
        busy_wait_us_32(50);
    }
}

void ps1_bus_release_pause(void) {
    __atomic_store_n(&ps1_pause_requested, false, __ATOMIC_RELEASE);

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

static void __not_in_flash_func(ps1_data_release)(void) {
    gpio_set_dir(PS1_DATA_PIN, GPIO_IN);
}

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

static void __not_in_flash_func(ps1_ack_release)(void) {
    gpio_set_dir(PS1_ACK_PIN, GPIO_IN);
}

static void __not_in_flash_func(ps1_ack_drive_low)(void) {
    gpio_put(PS1_ACK_PIN, 0);
    gpio_set_dir(PS1_ACK_PIN, GPIO_OUT);
}

void __not_in_flash_func(ps1emu_release_lines)(void) {
    ps1_data_release();
    ps1_ack_release();
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
        bool send_ack,
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

        ps1_bus_xfer_result_t wait_result = wait_sck_level(0, 10000);
        if (wait_result != PS1_BUS_XFER_OK) {
            if (result != NULL) {
                *result = wait_result;
            }
            ps1emu_release_lines();
            return rx;
        }

        wait_result = wait_sck_level(1, 10000);
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

    if (send_ack && gpio_get(PS1_CS_PIN) == 0) {
        ps1emu_ack_pulse();
    }

    if (result != NULL) {
        *result = PS1_BUS_XFER_OK;
    }

    return rx;
}

static ps1_bus_xfer_result_t __not_in_flash_func(ps1emu_xfer)(
        uint8_t tx,
        uint8_t *rx,
        bool send_ack) {
    ps1_bus_xfer_result_t result;
    uint8_t value = ps1emu_recv_send_byte_internal(tx,
                                                   send_ack,
                                                   &result);

    if (rx) {
        *rx = value;
    }

    return result;
}

static uint8_t __not_in_flash_func(ps1emu_recv_send_byte)(uint8_t tx) {
    uint8_t rx = 0;
    (void)ps1emu_xfer(tx, &rx, true);
    return rx;
}

static uint8_t __not_in_flash_func(ps1emu_recv_byte_no_ack)(uint8_t tx,
        ps1_bus_xfer_result_t *result) {
    uint8_t rx = 0;
    ps1_bus_xfer_result_t xfer_result = ps1emu_xfer(tx, &rx, false);

    if (result != NULL) {
        *result = xfer_result;
    }

    return rx;
}

static void __not_in_flash_func(ps1emu_send_dummy_bytes)(uint8_t value,
                                                         int count) {
    for (int i = 0; i < count; ++i) {
        (void)ps1emu_recv_send_byte(value);
    }
}

static void __not_in_flash_func(ps1emu_handle_read)(void) {
    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);

    uint8_t addr_msb = ps1emu_recv_send_byte(0x00);
    uint8_t addr_lsb = ps1emu_recv_send_byte(0x00);
    uint16_t frame_addr = ((uint16_t)addr_msb << 8) | addr_lsb;
    uint8_t *frame = get_frame_ptr(frame_addr);

    (void)ps1emu_recv_send_byte(0x5C);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(addr_msb);
    (void)ps1emu_recv_send_byte(addr_lsb);

    if (frame == NULL) {
        ps1emu_send_dummy_bytes(0xFF, PS1_FRAME_SIZE);
        (void)ps1emu_recv_send_byte(0xFF);
        (void)ps1emu_recv_send_byte(PS1_MC_ACK_BAD_SECTOR);
        return;
    }

    uint8_t checksum = ps1_frame_checksum(frame_addr, frame);

    for (int i = 0; i < PS1_FRAME_SIZE; ++i) {
        (void)ps1emu_recv_send_byte(frame[i]);
    }

    (void)ps1emu_recv_send_byte(checksum);
    (void)ps1emu_recv_send_byte(PS1_MC_ACK_GOOD);
}

static void __not_in_flash_func(ps1emu_handle_write)(void) {
    uint8_t data[PS1_FRAME_SIZE];

    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);

    uint8_t addr_msb = ps1emu_recv_send_byte(0x00);
    uint8_t addr_lsb = ps1emu_recv_send_byte(0x00);
    uint16_t frame_addr = ((uint16_t)addr_msb << 8) | addr_lsb;

    for (int i = 0; i < PS1_FRAME_SIZE; ++i) {
        data[i] = ps1emu_recv_send_byte(0x00);
    }

    uint8_t received_checksum = ps1emu_recv_send_byte(0x00);
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

    (void)ps1emu_recv_send_byte(0x5C);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(result);
}

static void __not_in_flash_func(ps1emu_handle_status)(void) {
    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(0x5C);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(0x04);
    (void)ps1emu_recv_send_byte(0x00);
    (void)ps1emu_recv_send_byte(0x00);
    (void)ps1emu_recv_send_byte(0x80);
}

void __not_in_flash_func(ps1emu_handle_transaction)(void) {
    ps1_bus_xfer_result_t result;
    uint8_t access = ps1emu_recv_byte_no_ack(0xFF, &result);

    if (result != PS1_BUS_XFER_OK || access != 0x81) {
        ps1emu_release_lines();
        return;
    }

    ps1emu_ack_pulse();

    uint8_t status_snapshot = ps1_mc_status;
    uint8_t command = ps1emu_recv_send_byte_internal(status_snapshot,
                                                      true,
                                                      &result);

    ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_WRITE_ERR;

    if (result != PS1_BUS_XFER_OK) {
        ps1emu_release_lines();
        return;
    }

    if (command == 0x52) {
        ps1emu_handle_read();
    } else if (command == 0x57) {
        ps1emu_handle_write();
    } else if (command == 0x53) {
        ps1emu_handle_status();
    }

    ps1emu_release_lines();
}
