#include "ps1_card_bus.h"

#include "hardware_config.h"
#include "ps1_card_emulator.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PS1_MC_STATUS_POWER_ON 0x08u
#define PS1_MC_STATUS_WRITE_ERR 0x04u
#define PS1_MC_ACK_GOOD 0x47u
#define PS1_MC_ACK_ERROR 0x43u
#define PS1_MC_ACK_BAD_SECTOR 0xFFu

static volatile uint8_t ps1_mc_status = PS1_MC_STATUS_POWER_ON;

bool ps1_bus_idle(void) {
    return gpio_get(PS1_CS_PIN) == 1;
}

static void ps1_data_release(void) {
    gpio_set_dir(PS1_DATA_PIN, GPIO_IN);
}

static void ps1_data_drive_low(void) {
    gpio_put(PS1_DATA_PIN, 0);
    gpio_set_dir(PS1_DATA_PIN, GPIO_OUT);
}

static void ps1_data_write_bit(uint8_t bit) {
    if (bit) {
        ps1_data_release();      
    } else {
        ps1_data_drive_low();    
    }
}

static void ps1_ack_release(void) {
    gpio_set_dir(PS1_ACK_PIN, GPIO_IN);
}

static void ps1_ack_drive_low(void) {
    gpio_put(PS1_ACK_PIN, 0);
    gpio_set_dir(PS1_ACK_PIN, GPIO_OUT);
}

void ps1emu_release_lines(void) {
    ps1_data_release();
    ps1_ack_release();
}

static void ps1emu_ack_pulse(void) {
    ps1_ack_drive_low();
    busy_wait_us_32(PS1_ACK_PULSE_US);
    ps1_ack_release();
}

static bool wait_sck_level(int level, uint32_t timeout_loops) {
    while (gpio_get(PS1_SCK_PIN) != level) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            return false;
        }

        if (--timeout_loops == 0) {
            return false;
        }

        tight_loop_contents();
    }

    return true;
}

static uint8_t ps1emu_recv_send_byte_internal(uint8_t tx, bool send_ack, bool *ok) {
    uint8_t rx = 0;

    if (ok) {
        *ok = false;
    }

    for (int bit = 0; bit < 8; bit++) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            ps1emu_release_lines();
            return rx;
        }

        ps1_data_write_bit((tx >> bit) & 1u);

        if (!wait_sck_level(0, 10000)) {
            ps1emu_release_lines();
            return rx;
        }

        if (!wait_sck_level(1, 10000)) {
            ps1emu_release_lines();
            return rx;
        }

        if (gpio_get(PS1_CMD_PIN)) {
            rx |= (1u << bit);
        }
    }

    if (send_ack && gpio_get(PS1_CS_PIN) == 0) {
        ps1emu_ack_pulse();
    }

    if (ok) {
        *ok = true;
    }

    return rx;
}

static bool ps1emu_xfer(uint8_t tx, uint8_t *rx, bool send_ack) {
    bool ok = false;
    uint8_t value = ps1emu_recv_send_byte_internal(tx, send_ack, &ok);

    if (rx) {
        *rx = value;
    }

    return ok;
}

static uint8_t ps1emu_recv_send_byte(uint8_t tx) {
    uint8_t rx = 0;
    (void)ps1emu_xfer(tx, &rx, true);
    return rx;
}

static uint8_t ps1emu_recv_byte_no_ack(uint8_t tx, bool *ok) {
    uint8_t rx = 0;
    bool good = ps1emu_xfer(tx, &rx, false);

    if (ok) {
        *ok = good;
    }

    return rx;
}

static void ps1emu_send_dummy_bytes(uint8_t value, int count) {
    for (int i = 0; i < count; i++) {
        (void)ps1emu_recv_send_byte(value);
    }
}

static void ps1emu_handle_read(void) {
    uint8_t addr_msb;
    uint8_t addr_lsb;

    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);

    addr_msb = ps1emu_recv_send_byte(0x00);
    addr_lsb = ps1emu_recv_send_byte(0x00);

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

    for (int i = 0; i < PS1_FRAME_SIZE; i++) {
        (void)ps1emu_recv_send_byte(frame[i]);
    }

    (void)ps1emu_recv_send_byte(checksum);
    (void)ps1emu_recv_send_byte(PS1_MC_ACK_GOOD);
}

static void ps1emu_handle_write(void) {
    uint8_t addr_msb;
    uint8_t addr_lsb;
    uint8_t data[PS1_FRAME_SIZE];

    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);

    addr_msb = ps1emu_recv_send_byte(0x00);
    addr_lsb = ps1emu_recv_send_byte(0x00);

    uint16_t frame_addr = ((uint16_t)addr_msb << 8) | addr_lsb;

    for (int i = 0; i < PS1_FRAME_SIZE; i++) {
        data[i] = ps1emu_recv_send_byte(0x00);
    }

    uint8_t received_checksum = ps1emu_recv_send_byte(0x00);
    uint8_t calculated_checksum = ps1_frame_checksum(frame_addr, data);
    uint8_t *frame = get_frame_ptr(frame_addr);
    uint8_t result = PS1_MC_ACK_GOOD;

    if (frame == NULL) {
        ps1_mc_status |= PS1_MC_STATUS_WRITE_ERR;
        result = PS1_MC_ACK_BAD_SECTOR;
    } else if (received_checksum != calculated_checksum) {
        ps1_mc_status |= PS1_MC_STATUS_WRITE_ERR;
        result = PS1_MC_ACK_ERROR;
    } else {
        memcpy(frame, data, PS1_FRAME_SIZE);
        card_dirty = true;
        dirty_counter++;
        last_write_time = get_absolute_time();
        ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_POWER_ON;
        ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_WRITE_ERR;
    }

     
    (void)ps1emu_recv_send_byte(0x5C);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(result);
}

static void ps1emu_handle_status(void) {
    // CMD: 81 53 00 00 00 00 00 00 00 00
    // DAT: -- FL 5A 5D 5C 5D 04 00 00 80
    (void)ps1emu_recv_send_byte(0x5A);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(0x5C);
    (void)ps1emu_recv_send_byte(0x5D);
    (void)ps1emu_recv_send_byte(0x04);
    (void)ps1emu_recv_send_byte(0x00);
    (void)ps1emu_recv_send_byte(0x00);
    (void)ps1emu_recv_send_byte(0x80);
}

void ps1emu_handle_transaction(void) {
    bool ok;
    uint8_t access;
    uint8_t command;
    uint8_t status_snapshot;

    access = ps1emu_recv_byte_no_ack(0xFF, &ok);

    if (!ok) {
        ps1emu_release_lines();
        return;
    }

    if (access != 0x81) {
        ps1emu_release_lines();
        return;
    }

    ps1emu_ack_pulse();

    status_snapshot = ps1_mc_status;
    command = ps1emu_recv_send_byte_internal(status_snapshot, true, &ok);

    
    ps1_mc_status &= (uint8_t)~PS1_MC_STATUS_WRITE_ERR;

    if (!ok) {
        ps1emu_release_lines();
        return;
    }

    if (command == 0x52) {
        ps1emu_handle_read();
    } else if (command == 0x57) {
        ps1emu_handle_write();
    } else if (command == 0x53) {
        ps1emu_handle_status();
    } else {
        printf("Unknown memory card command: 0x%02X\n", command);
    }

    ps1emu_release_lines();
}
