#ifndef PIPELINE_TEST_SUPPORT_H
#define PIPELINE_TEST_SUPPORT_H

#include "ff.h"
#include "ps1/ps1_card_geometry.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIPELINE_IMAGE_A_PATH "0:/CARD_A.MCR"
#define PIPELINE_IMAGE_B_PATH "0:/CARD_B.MCR"

void pipeline_test_set_up(void);
void pipeline_test_tear_down(void);

uint8_t *pipeline_add_valid_image(const char *path);
const uint8_t *pipeline_durable_image_data(const char *path);
void pipeline_fill_pattern(uint8_t data[PS1_FRAME_SIZE], uint8_t seed);
void pipeline_initialize_image_a(void);
void pipeline_restart_firmware(const char *path);
void pipeline_reconnect_storage(void);

void pipeline_console_write(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]);
void pipeline_prepare_write(
        uint16_t frame_addr,
        const uint8_t data[PS1_FRAME_SIZE]);
void pipeline_prepare_read(uint16_t frame_addr);
bool pipeline_run_console_transaction(void);
const uint8_t *pipeline_read_response_data(void);
size_t pipeline_script_position(void);

void pipeline_assert_frame_is_dirty(
        uint16_t expected_addr,
        const uint8_t expected_data[PS1_FRAME_SIZE]);
void pipeline_assert_no_dirty_frames(void);
void pipeline_finish_card_swap_delay(void);

void pipeline_fail_next_write(void);
void pipeline_fail_write_on_call(uint32_t call_number);
void pipeline_fail_next_sync(void);
void pipeline_remove_sd_during_next_write(UINT partial_write_count);
void pipeline_fail_next_read_partially(const char *path,
                                       UINT partial_read_count);
void pipeline_set_storage_delays(int64_t write_delay_us,
                                 int64_t sync_delay_us);
void pipeline_set_physical_card_present(bool present);
bool pipeline_physical_card_present(void);
void pipeline_advance_time_us(int64_t delta_us);

uint32_t pipeline_write_count(void);
uint32_t pipeline_sync_count(void);
uint32_t pipeline_fatfs_reset_count(void);
uint32_t pipeline_mount_count(void);
FSIZE_t pipeline_last_write_offset(void);
UINT pipeline_last_write_size(void);

#endif // PIPELINE_TEST_SUPPORT_H
