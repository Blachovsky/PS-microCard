#ifndef MENU_CARD_MONITOR_H
#define MENU_CARD_MONITOR_H

#include <stdbool.h>

typedef enum {
    MENU_CARD_EVENT_NONE = 0,
    MENU_CARD_EVENT_INSERTED,
    MENU_CARD_EVENT_REMOVED,
    MENU_CARD_EVENT_RETRY_DUE,
    MENU_CARD_EVENT_PROBE_DUE,
} menu_card_event_t;

void menu_card_monitor_init(void);
menu_card_event_t menu_card_monitor_poll(void);
bool menu_card_monitor_is_present(void);
bool menu_card_monitor_is_ready(void);
bool menu_card_monitor_is_physically_present(void);
void menu_card_monitor_mark_ready(void);
void menu_card_monitor_mark_error(void);
void menu_card_monitor_mark_removed(void);

#endif // MENU_CARD_MONITOR_H
