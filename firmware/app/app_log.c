#include "app/app_log.h"

#include "pico/mutex.h"
#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

static mutex_t log_mutex;
static bool log_initialized;

static const char *log_level_name(app_log_level_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR:
            return "ERROR";
        case LOG_LEVEL_WARNING:
            return "WARN";
        case LOG_LEVEL_INFO:
            return "INFO";
        case LOG_LEVEL_DEBUG:
            return "DEBUG";
        default:
            return "UNKNOWN";
    }
}

void app_log_init(void) {
    if (log_initialized) {
        return;
    }

    mutex_init(&log_mutex);
    log_initialized = true;
}

void app_log_write(app_log_level_t level,
                   const char *module,
                   const char *format,
                   ...) {
    if (module == NULL || format == NULL) {
        return;
    }

    if (log_initialized) {
        mutex_enter_blocking(&log_mutex);
    }

    printf("[%10lu] [C%u] [%-5s] [%s] ",
           (unsigned long)to_ms_since_boot(get_absolute_time()),
           get_core_num(),
           log_level_name(level),
           module);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');

    if (log_initialized) {
        mutex_exit(&log_mutex);
    }
}
