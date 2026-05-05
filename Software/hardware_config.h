#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <stddef.h>
#include "sd_card.h"

// =====================
// microSD SPI pins
// =====================
#define SD_SPI_PORT     spi0
#define SD_MISO_PIN     16
#define SD_MOSI_PIN     19
#define SD_SCK_PIN      18
#define SD_CS_PIN       17

#define SD_BAUD_RATE    (1000 * 1000)

// =====================
// PS1 Memory Card pins
// =====================
#define PS1_SCK_PIN     10
#define PS1_CMD_PIN     11
#define PS1_DATA_PIN    12
#define PS1_CS_PIN      13
#define PS1_ACK_PIN     14

#define PS1_FRAME_SIZE  128
#define PS1_FRAME_COUNT 1024
#define PS1_CARD_SIZE (PS1_FRAME_COUNT*PS1_FRAME_SIZE)

#define BACKUP_PATH "0:/CARD001.MCR"
#define RESTORE_PATH "0:/CARD000_restored.MCR"

#
// =====================
// Functions required by the SD library
// =====================
size_t sd_get_num(void);
sd_card_t *sd_get_by_num(size_t num);

#endif 