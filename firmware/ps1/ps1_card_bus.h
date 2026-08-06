#ifndef PS1_CARD_BUS_H
#define PS1_CARD_BUS_H

/* PS1 serial bus transaction control. */

#include <stdbool.h>

void ps1emu_handle_transaction(void);
void ps1emu_release_lines(void);
void ps1_bus_service_pause_if_requested(void);
void ps1_bus_request_pause_blocking(void);
void ps1_bus_release_pause(void);
void ps1_bus_set_card_present(bool present);
void ps1_bus_begin_card_swap_absent(void);
bool ps1_bus_should_ignore_transaction_for_swap(void);

#endif // PS1_CARD_BUS_H
