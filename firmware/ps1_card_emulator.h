#ifndef PS1_CARD_EMULATOR_H
#define PS1_CARD_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware_config.h"

extern uint8_t card_image[PS1_CARD_SIZE];

/*
 * Initializes the version markers shared between cores.
 * Call once before starting core 1 and before servicing the PS1 bus.
 */
void ps1emu_storage_state_init(void);

/*
 * Writes one frame into the RAM card image. This function runs only on core 0.
 * On success it wakes core 1 with SEV.
 */
bool ps1emu_commit_frame(uint16_t frame_addr,
                         const uint8_t data[PS1_FRAME_SIZE]);

/*
 * Returns a consistent copy of a frame changed since the last core 1 fetch.
 * The frame version is needed to confirm durable storage after f_sync().
 */
bool ps1emu_take_changed_frame(uint16_t *frame_addr,
                               uint32_t *frame_version,
                               uint8_t data[PS1_FRAME_SIZE]);

/* Confirms that a specific frame version has been synced to microSD. */
void ps1emu_confirm_frame_synced(uint16_t frame_addr,
                                 uint32_t frame_version);

/*
 * Rolls fetched state back to the last versions confirmed by f_sync().
 * Used after f_write(), f_sync(), or f_close() errors.
 */
void ps1emu_rollback_unconfirmed_frames(void);

uint8_t *get_frame_ptr(uint16_t frame_addr);
uint8_t ps1_frame_checksum(uint16_t frame_addr,
                           const uint8_t data[PS1_FRAME_SIZE]);

#endif // PS1_CARD_EMULATOR_H
