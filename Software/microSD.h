#ifndef MICROSD_H
#define MICROSD_H

#include <stdbool.h>

// Test functions
bool write_test_file(void);
bool read_test_file(void);
bool test_backup_ps1_card_to_microsd(const char *path);
void test_microsd(void);
 
#endif