#include "hardware_config.h"
#include "microSD.h"
#include "ps1_card_bus.h"
#include "ps1_card_emulator.h"

#include "pico/stdlib.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IMAGE_PATH "0:/CARD000.MCR"
#define SAVE_IDLE_DELAY_MS 250u
#define SAVE_RETRY_DELAY_MS 1000u

static bool wait_cs_released_timeout(uint32_t timeout_us) {
    absolute_time_t deadline = make_timeout_time_us(timeout_us);

    while (!time_reached(deadline)) {
        if (gpio_get(PS1_CS_PIN) == 1) {
            return true;
        }

        tight_loop_contents();
    }

    return false;
}

static void maybe_save_card_image(void) {
    static uint32_t saved_dirty_counter = 0;
    static absolute_time_t last_save_attempt;

    if (!card_dirty) {
        return;
    }

    if (!ps1_bus_idle()) {
        return;
    }

    absolute_time_t now = get_absolute_time();

    if (absolute_time_diff_us(last_write_time, now) < (int64_t)SAVE_IDLE_DELAY_MS * 1000) {
        return;
    }

    if (absolute_time_diff_us(last_save_attempt, now) < (int64_t)SAVE_RETRY_DELAY_MS * 1000) {
        return;
    }

    last_save_attempt = now;

    uint32_t dirty_snapshot = dirty_counter;

    if (save_card_image_to_sd(IMAGE_PATH)) {
        saved_dirty_counter = dirty_snapshot;

        if (dirty_counter == saved_dirty_counter) {
            card_dirty = false;
        }
    }
}

void print_card_image(const uint8_t *card, size_t size) {
    for (size_t offset = 0; offset < size; offset += 16) {
        printf("%08zx  ", offset);

        for (size_t i = 0; i < 16; i++) {
            if (offset + i < size) {
                printf("%02X ", card[offset + i]);
            } else {
                printf("   ");
            }

            if (i == 7) {
                printf(" ");
            }
        }

        printf(" |");

        for (size_t i = 0; i < 16; i++) {
            if (offset + i < size) {
                uint8_t c = card[offset + i];
                printf("%c", isprint(c) ? c : '.');
            } else {
                printf(" ");
            }
        }

        printf("|\n");
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1000);

    printf("\nPS1 memory card emulator\n");

    ps1emu_gpio_init();
    ps1emu_release_lines();

    printf("GPIO initialized\n");
    printf("Initial bus state: CS=%d CLK=%d CMD=%d DATA=%d ACK=%d\n",
           gpio_get(PS1_CS_PIN),
           gpio_get(PS1_SCK_PIN),
           gpio_get(PS1_CMD_PIN),
           gpio_get(PS1_DATA_PIN),
           gpio_get(PS1_ACK_PIN));

    if (!load_card_image_from_sd(IMAGE_PATH)) {
        printf("Could not load card image\n");

        while (true) {
            sleep_ms(1000);
        }
    }

    printf("Waiting for PS1...\n");

    bool prev_cs = true;

    while (true) {
        bool cs = gpio_get(PS1_CS_PIN);

        if (prev_cs && !cs) {
            ps1emu_handle_transaction();
            ps1emu_release_lines();

            (void)wait_cs_released_timeout(5000);
            prev_cs = gpio_get(PS1_CS_PIN);
        } else {
            prev_cs = cs;
        }

        //maybe_save_card_image();
        tight_loop_contents();
    }
}
