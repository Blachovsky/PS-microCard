#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <stddef.h>

#include "sd_card.h"

// =====================
// microSD SPI0 pins
// =====================
#define SD_SPI_PORT     spi0
#define SD_MISO_PIN     16
#define SD_MOSI_PIN     19
#define SD_SCK_PIN      18
#define SD_CS_PIN       17
#define SD_DETECT_PIN   20

// Pololu 2597 CD is pulled high when a card is present and grounded when empty.
#define SD_DETECT_PRESENT_LEVEL 1

#define SD_BAUD_RATE    (8 * 1000 * 1000)

// =====================
// DFR0650 OLED / SSD1306 on SPI1
// =====================
// DFR0650 labels: SCL = SCK, SDA = MOSI.
#define OLED_SPI_PORT   spi1
#define OLED_SCK_PIN    10
#define OLED_MOSI_PIN   11
#define OLED_DC_PIN     12
#define OLED_CS_PIN     13

#define OLED_BAUD_RATE  (8 * 1000 * 1000)
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Set to 1 if the display is mounted upside down.
#define OLED_ROTATE_180 0

// =====================
// Two-button menu
// =====================
// Tact switches should connect the GPIO pin to GND. Internal pull-ups are used.
#define MENU_NEXT_PIN   8
#define MENU_SELECT_PIN 9

// =====================
// PS1 Memory Card pins
// =====================
#define PS1_SCK_PIN     2
#define PS1_CMD_PIN     3
#define PS1_DATA_PIN    4
#define PS1_CS_PIN      5
#define PS1_ACK_PIN     6

#define PS1_FRAME_SIZE  128
#define PS1_FRAME_COUNT 1024
#define PS1_CARD_SIZE   (PS1_FRAME_COUNT * PS1_FRAME_SIZE)

#define PS1_ACK_PULSE_US     3
#define PS1_SAMPLE_DELAY_US  2

// =====================
// Functions required by PS1 pins
// =====================
void ps1emu_gpio_init(void);

// =====================
// Functions required by the SD library
// =====================
size_t sd_get_num(void);
sd_card_t *sd_get_by_num(size_t num);

#endif // HARDWARE_CONFIG_H
