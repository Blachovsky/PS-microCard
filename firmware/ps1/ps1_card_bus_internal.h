#ifndef PS1_CARD_BUS_INTERNAL_H
#define PS1_CARD_BUS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define PS1_BUS_CLOCK_TIMEOUT_LOOPS 10000u

typedef enum {
    PS1_BUS_XFER_OK = 0,
    PS1_BUS_XFER_ABORTED,
    PS1_BUS_XFER_CLOCK_TIMEOUT,
} ps1_bus_xfer_result_t;

#ifdef UNIT_TEST
typedef ps1_bus_xfer_result_t (*ps1_bus_test_xfer_fn_t)(
        uint8_t tx,
        uint8_t *rx,
        bool ack_before);

void ps1_bus_test_reset_state(void);
void ps1_bus_test_set_transport(ps1_bus_test_xfer_fn_t xfer_fn);
void ps1_bus_test_set_pause_auto_ack(bool enabled);
ps1_bus_xfer_result_t ps1_bus_test_hardware_xfer(uint8_t tx,
                                                uint8_t *rx,
                                                bool ack_after);
#endif

#endif // PS1_CARD_BUS_INTERNAL_H
