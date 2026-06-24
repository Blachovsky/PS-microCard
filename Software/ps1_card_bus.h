#ifndef PS1_CARD_BUS_H
#define PS1_CARD_BUS_H

#include <stdbool.h>

void ps1emu_handle_transaction(void);
void ps1emu_release_lines(void);
bool ps1_bus_idle(void);

#endif // PS1_CARD_BUS_H
