#include "ps1/ps1_card_emulator.h"

#include "pico/stdlib.h"

#include <stddef.h>
#include <string.h>

uint8_t card_image[PS1_CARD_SIZE];

/*
 * frame_sequence is written only by core 0 and read by core 1. An odd value
 * means a frame copy is in progress; an even value means the image is stable.
 * Core 1 owns the observed/confirmed arrays and never writes frame_sequence,
 * so the PS1 hot path does not need a lock or spinlock.
 */
static uint32_t frame_sequence[PS1_FRAME_COUNT];
static uint32_t observed_sequence[PS1_FRAME_COUNT];
static uint32_t confirmed_sequence[PS1_FRAME_COUNT];
static uint16_t scan_cursor;

void ps1emu_storage_state_init(void) {
    memset(frame_sequence, 0, sizeof(frame_sequence));
    memset(observed_sequence, 0, sizeof(observed_sequence));
    memset(confirmed_sequence, 0, sizeof(confirmed_sequence));
    scan_cursor = 0;
}

uint8_t *__not_in_flash_func(get_frame_ptr)(uint16_t frame_addr) {
    if (frame_addr >= PS1_FRAME_COUNT) {
        return NULL;
    }

    return &card_image[(size_t)frame_addr * PS1_FRAME_SIZE];
}

/*
 * volatile on the destination pointer prevents the compiler from replacing
 * this small loop with a library memcpy call located in Flash.
 */
static void __not_in_flash_func(copy_frame_to_card)(
        uint8_t *dst,
        const uint8_t src[PS1_FRAME_SIZE]) {
    volatile uint8_t *volatile_dst = dst;

    for (size_t i = 0; i < PS1_FRAME_SIZE; ++i) {
        volatile_dst[i] = src[i];
    }
}

ps1emu_result_t __not_in_flash_func(ps1emu_commit_frame)(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]) {
    if (data == NULL) {
        return PS1EMU_ERROR_INVALID_ARGUMENT;
    }

    uint8_t *frame = get_frame_ptr(frame_addr);
    if (frame == NULL) {
        return PS1EMU_ERROR_FRAME_OUT_OF_RANGE;
    }

    uint32_t sequence = __atomic_load_n(&frame_sequence[frame_addr],
                                        __ATOMIC_RELAXED);

    /* Core 0 is the only producer, so sequence is normally even. */
    if (sequence & 1u) {
        ++sequence;
    }

    __atomic_store_n(&frame_sequence[frame_addr],
                     sequence + 1u,
                     __ATOMIC_SEQ_CST);

    copy_frame_to_card(frame, data);

    __atomic_store_n(&frame_sequence[frame_addr],
                     sequence + 2u,
                     __ATOMIC_SEQ_CST);

    /* Wake core 1 if it is waiting in WFE for the first change. */
    __sev();
    return PS1EMU_RESULT_OK;
}

static ps1emu_result_t snapshot_frame(
        uint16_t frame_addr,
        uint32_t *stable_version,
        uint8_t data[PS1_FRAME_SIZE]) {
    if (stable_version == NULL || data == NULL) {
        return PS1EMU_ERROR_INVALID_ARGUMENT;
    }

    if (frame_addr >= PS1_FRAME_COUNT) {
        return PS1EMU_ERROR_FRAME_OUT_OF_RANGE;
    }

    const uint8_t *frame = &card_image[(size_t)frame_addr * PS1_FRAME_SIZE];

    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        uint32_t before = __atomic_load_n(&frame_sequence[frame_addr],
                                          __ATOMIC_ACQUIRE);

        if (before & 1u) {
            continue;
        }

        memcpy(data, frame, PS1_FRAME_SIZE);

        uint32_t after = __atomic_load_n(&frame_sequence[frame_addr],
                                         __ATOMIC_ACQUIRE);

        if (before == after && !(after & 1u)) {
            *stable_version = after;
            return PS1EMU_RESULT_OK;
        }
    }

    return PS1EMU_ERROR_SNAPSHOT_BUSY;
}

ps1emu_result_t ps1emu_take_changed_frame(
        uint16_t *frame_addr,
        uint32_t *frame_version,
        uint8_t data[PS1_FRAME_SIZE]) {
    if (frame_addr == NULL || frame_version == NULL || data == NULL) {
        return PS1EMU_ERROR_INVALID_ARGUMENT;
    }

    for (uint16_t checked = 0; checked < PS1_FRAME_COUNT; ++checked) {
        uint16_t candidate = scan_cursor;
        scan_cursor = (uint16_t)((scan_cursor + 1u) % PS1_FRAME_COUNT);

        uint32_t current = __atomic_load_n(&frame_sequence[candidate],
                                           __ATOMIC_ACQUIRE);

        if ((current & 1u) || current == observed_sequence[candidate]) {
            continue;
        }

        uint32_t stable_version;

        ps1emu_result_t result = snapshot_frame(candidate,
                                                &stable_version,
                                                data);
        if (result == PS1EMU_ERROR_SNAPSHOT_BUSY) {
            continue;
        }

        if (result != PS1EMU_RESULT_OK) {
            return result;
        }

        if (stable_version == observed_sequence[candidate]) {
            continue;
        }

        observed_sequence[candidate] = stable_version;
        *frame_addr = candidate;
        *frame_version = stable_version;
        return PS1EMU_RESULT_OK;
    }

    return PS1EMU_RESULT_NO_CHANGED_FRAME;
}

void ps1emu_confirm_frame_synced(uint16_t frame_addr,
                                 uint32_t frame_version) {
    if (frame_addr >= PS1_FRAME_COUNT) {
        return;
    }

    confirmed_sequence[frame_addr] = frame_version;
}

void ps1emu_rollback_unconfirmed_frames(void) {
    memcpy(observed_sequence, confirmed_sequence, sizeof(observed_sequence));
    scan_cursor = 0;
    __sev();
}

uint8_t __not_in_flash_func(ps1_frame_checksum)(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]) {
    uint8_t checksum = 0;

    checksum ^= (uint8_t)(frame_addr & 0xFFu);
    checksum ^= (uint8_t)((frame_addr >> 8) & 0xFFu);

    for (int i = 0; i < PS1_FRAME_SIZE; ++i) {
        checksum ^= data[i];
    }

    return checksum;
}
