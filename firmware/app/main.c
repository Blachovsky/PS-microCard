#include "app/app_log.h"
#include "board/hardware_config.h"
#include "menu/menu.h"
#include "ps1/ps1_card_bus.h"
#include "ps1/ps1_card_emulator.h"

#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>

#define LOG_TAG "main"

#define IMAGE_PATH "0:/CARD000.MCR"
#define CORE1_STACK_SIZE_BYTES 8192u

/* FatFs needs more stack than the default 0x800 bytes on core 1. */
static uint32_t core1_stack[CORE1_STACK_SIZE_BYTES / sizeof(uint32_t)]
        __attribute__((aligned(8)));

static void core1_storage_entry(void) {
    menu_task_run(IMAGE_PATH);
}

static void __not_in_flash_func(wait_for_cs_release_bounded)(void) {
    uint32_t deadline = time_us_32() + 5000u;

    while (gpio_get(PS1_CS_PIN) == 0) {
        if ((int32_t)(time_us_32() - deadline) >= 0) {
            break;
        }

        tight_loop_contents();
    }
}

/*
 * The whole infinite core 0 loop is copied to SRAM. Core 1 can then execute
 * FatFs from XIP/Flash without stalling instruction fetch for the code that
 * tracks CS and CLK.
 */
static void __not_in_flash_func(ps1_bus_service_loop)(void) {
    while (gpio_get(PS1_CS_PIN) == 0) {
        tight_loop_contents();
    }

    bool prev_cs = true;

    while (true) {
        ps1_bus_service_pause_if_requested();

        bool cs = gpio_get(PS1_CS_PIN);

        if (prev_cs && !cs) {
            /* USB/timer IRQs must not interrupt a single PS1 transaction. */
            uint32_t irq_state = save_and_disable_interrupts();

            if (!ps1_bus_should_ignore_transaction_for_swap()) {
                ps1emu_handle_transaction();
            }

            ps1emu_release_lines();
            wait_for_cs_release_bounded();

            restore_interrupts(irq_state);
            prev_cs = gpio_get(PS1_CS_PIN);
        } else {
            prev_cs = cs;
        }

        tight_loop_contents();
    }
}

int main(void) {
    stdio_init_all();
    app_log_init();
    sleep_ms(1000);

    LOG_INFO(LOG_TAG,
             "PS1 memory card emulator - dual core + DFR0650 OLED");

    ps1emu_gpio_init();
    ps1emu_release_lines();
    ps1emu_storage_state_init();
    ps1_bus_set_card_present(false);

    LOG_INFO(LOG_TAG, "GPIO initialized");
    LOG_DEBUG(LOG_TAG,
              "Initial bus state: CS=%d CLK=%d CMD=%d DATA=%d ACK=%d",
              gpio_get(PS1_CS_PIN),
              gpio_get(PS1_SCK_PIN),
              gpio_get(PS1_CMD_PIN),
              gpio_get(PS1_DATA_PIN),
              gpio_get(PS1_ACK_PIN));

    multicore_launch_core1_with_stack(core1_storage_entry,
                                      core1_stack,
                                      sizeof(core1_stack));

    LOG_INFO(LOG_TAG, "Storage worker started on core 1");
    LOG_INFO(LOG_TAG, "Waiting for PS1 on core 0");

    ps1_bus_service_loop();
    return 0;
}
