#ifndef MICRO_SD_INTERNAL_H
#define MICRO_SD_INTERNAL_H

#include "micro_sd/micro_sd.h"

#include <stddef.h>

char micro_sd_internal_ascii_upper(char c);
bool micro_sd_internal_names_equal_ignore_case(const char *a, const char *b);
void micro_sd_internal_copy_string(char *dst,
                                   size_t dst_size,
                                   const char *src);
void micro_sd_internal_copy_name_from_path(const char *path,
                                           char *name,
                                           size_t name_size);
void micro_sd_internal_name_to_path(const char *name,
                                    char *path,
                                    size_t path_size);

void micro_sd_internal_set_active_image_path(const char *path);
micro_sd_result_t micro_sd_internal_mount(void);
micro_sd_result_t micro_sd_internal_storage_result(void);
void micro_sd_internal_set_storage_result(micro_sd_result_t result);
void micro_sd_internal_reset_fatfs_card_state(void);

void micro_sd_internal_worker_reset(void);

#endif // MICRO_SD_INTERNAL_H
