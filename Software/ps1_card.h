#ifndef PS1_CARD_H
#define PS1_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware_config.h"

// Initialize GPIO pins for the PS1 memory card
void ps1_mc_gpio_init(void);

// Transfer a single byte
uint8_t ps1_transfer_byte(uint8_t out);

// Wait for ACK from the card
bool ps1_wait_ack(uint32_t timeout_us);

// Transfer a byte with ACK handling
uint8_t ps1_transfer_byte_ack(uint8_t out, const char *label);

// Calculate frame checksum
uint8_t ps1_frame_checksum(uint16_t frame_addr, const uint8_t data[PS1_FRAME_SIZE]);

// Read a single frame from the PS1 memory card
bool ps1_mc_read_frame(uint16_t frame_addr, uint8_t out[PS1_FRAME_SIZE]);

// 5 attemps to read a single frame
bool ps1_mc_read_frame_retry(uint16_t frame_no, uint8_t out[PS1_FRAME_SIZE]); 

// Write a single frame to the PS1 memory card
bool ps1_mc_write_frame(uint16_t frame_addr, uint8_t data[PS1_FRAME_SIZE]);

// 5 attemps to write a single frame
bool ps1_mc_write_frame_retry(uint16_t frame_no, uint8_t data[PS1_FRAME_SIZE]);

// Verify written frame
bool verify_written_frame(uint16_t frame_addr, const uint8_t expected[PS1_FRAME_SIZE]);

// Print frame data in HEX format
void dump_frame_hex(const uint8_t data[PS1_FRAME_SIZE]);

// Test reading multiple frames
void test_frames_read(const int no_of_frames);

#endif