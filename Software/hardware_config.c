#include "hardware_config.h"

#include "hw_config.h"
#include "pico/stdlib.h"

#include <stdbool.h>

// =====================
// PS1 Memory Card
// =====================
void ps1emu_gpio_init(void) {
    gpio_init(PS1_SCK_PIN);
    gpio_set_dir(PS1_SCK_PIN, GPIO_IN);
    gpio_pull_up(PS1_SCK_PIN);

    gpio_init(PS1_CMD_PIN);
    gpio_set_dir(PS1_CMD_PIN, GPIO_IN);
    gpio_pull_up(PS1_CMD_PIN);

    gpio_init(PS1_CS_PIN);
    gpio_set_dir(PS1_CS_PIN, GPIO_IN);
    gpio_pull_up(PS1_CS_PIN);

    gpio_init(PS1_DATA_PIN);
    gpio_put(PS1_DATA_PIN, 0);
    gpio_set_dir(PS1_DATA_PIN, GPIO_IN);
    gpio_disable_pulls(PS1_DATA_PIN);

    gpio_init(PS1_ACK_PIN);
    gpio_put(PS1_ACK_PIN, 0);
    gpio_set_dir(PS1_ACK_PIN, GPIO_IN);
    gpio_disable_pulls(PS1_ACK_PIN);
}

// =====================
// microSD SPI
// =====================
static spi_t spi = {
    .hw_inst = SD_SPI_PORT,
    .miso_gpio = SD_MISO_PIN,
    .mosi_gpio = SD_MOSI_PIN,
    .sck_gpio = SD_SCK_PIN,
    .baud_rate = SD_BAUD_RATE,
};

static sd_spi_if_t spi_if = {
    .spi = &spi,
    .ss_gpio = SD_CS_PIN,
};

static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if,
    .use_card_detect = true,
    .card_detect_gpio = SD_DETECT_PIN,
    .card_detected_true = SD_DETECT_PRESENT_LEVEL,
    .card_detect_use_pull = true,
    .card_detect_pull_hi = true,
};

size_t sd_get_num(void) {
    return 1;
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num == 0) {
        return &sd_card;
    }

    return NULL;
}
