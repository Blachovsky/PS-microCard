#ifndef MICROSD_H
#define MICROSD_H

#include <stdbool.h>

// Test functions
bool write_test_file(void);
bool read_test_file(void);
void test_microsd(void);
bool test_backup_ps1_card_to_microsd(const char *path);
bool test_restore_microsd_to_ps1_card(const char *path);
bool verify_backup_file_size(const char *path);
#endif