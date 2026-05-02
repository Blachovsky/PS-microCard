#include"hardware_config.h" //Local header file for pins
#include"hw_config.h" //SD library header

static spi_t spi = {
    .hw_inst = SD_SPI_PORT,
    .miso_gpio=SD_MISO_PIN,
    .mosi_gpio=SD_MOSI_PIN,
    .sck_gpio=SD_SCK_PIN,
    .baud_rate = SD_BAUD_RATE,
};

static sd_spi_if_t spi_if = {
    .spi = &spi,
    .ss_gpio = SD_CS_PIN,
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

