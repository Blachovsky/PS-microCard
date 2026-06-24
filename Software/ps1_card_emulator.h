#ifndef PS1_CARD_EMULATOR_H
#define PS1_CARD_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "pico/time.h"
#include "hardware_config.h"

extern absolute_time_t last_write_time;
extern uint8_t card_image[PS1_CARD_SIZE];
extern volatile bool card_dirty;
extern volatile uint32_t dirty_counter;

uint8_t *get_frame_ptr(uint16_t frame_addr);
uint8_t ps1_frame_checksum(uint16_t frame_addr, const uint8_t data[PS1_FRAME_SIZE]);

#endif // PS1_CARD_EMULATOR_H
