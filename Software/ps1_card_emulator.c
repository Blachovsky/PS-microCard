#include "ps1_card_emulator.h"

#include <stddef.h>

absolute_time_t last_write_time;
uint8_t card_image[PS1_CARD_SIZE];
volatile bool card_dirty = false;
volatile uint32_t dirty_counter = 0;

uint8_t *get_frame_ptr(uint16_t frame_addr) {
    if (frame_addr >= PS1_FRAME_COUNT) {
        return NULL;
    }

    return &card_image[frame_addr * PS1_FRAME_SIZE];
}

uint8_t ps1_frame_checksum(uint16_t frame_addr, const uint8_t data[PS1_FRAME_SIZE]) {
    uint8_t c = 0;
    uint8_t addr_lsb = frame_addr & 0xFF;
    uint8_t addr_msb = (frame_addr >> 8) & 0xFF;

    c ^= addr_lsb;
    c ^= addr_msb;

    for (int i = 0; i < PS1_FRAME_SIZE; i++) {
        c ^= data[i];
    }

    return c;
}
