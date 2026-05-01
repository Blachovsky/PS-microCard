#include"hw_config.h"

static spi_t spi = {
    .hw_inst = spi0,
    .miso_gpio=16,
    .mosi_gpio=19,
    .sck_gpio=18,
    .baud_rate = 1000 * 1000, //  1000000 Hz 
    //.baud_rate = 125 * 1000 * 1000 / 8,  // 15625000 Hz
    //.baud_rate = 125 * 1000 * 1000 / 6, // 20833333 Hz
    //.baud_rate = 125 * 1000 * 1000 / 4,  // 31250000 Hz
    //.baud_rate = 125 * 1000 * 1000 / 2, // 62500000 Hz
};

static sd_spi_if_t spi_if = {
    .spi = &spi,
    .ss_gpio = 17,
};

static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if,
    .use_card_detect = false,
};

size_t sd_get_num(void) {
    return 1;
}

sd_card_t *sd_get_by_num(size_t num){
    if (0 == num){
        return &sd_card;
    }
    return NULL;
}

