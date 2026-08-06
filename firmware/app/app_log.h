#ifndef APP_LOG_H
#define APP_LOG_H

/* Application-wide logging API. */

#define APP_LOG_LEVEL_NONE    (-1)
#define APP_LOG_LEVEL_ERROR   0
#define APP_LOG_LEVEL_WARNING 1
#define APP_LOG_LEVEL_INFO    2
#define APP_LOG_LEVEL_DEBUG   3

/* Change this value to select the application logging verbosity. */
#define APP_LOG_LEVEL APP_LOG_LEVEL_INFO

typedef enum {
    LOG_LEVEL_ERROR = APP_LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING = APP_LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO = APP_LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG = APP_LOG_LEVEL_DEBUG,
} app_log_level_t;

void app_log_init(void);

void app_log_write(app_log_level_t level,
                   const char *module,
                   const char *format,
                   ...) __attribute__((format(printf, 3, 4)));

#if APP_LOG_LEVEL >= APP_LOG_LEVEL_ERROR
#define LOG_ERROR(module, ...) \
    app_log_write(LOG_LEVEL_ERROR, module, __VA_ARGS__)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if APP_LOG_LEVEL >= APP_LOG_LEVEL_WARNING
#define LOG_WARNING(module, ...) \
    app_log_write(LOG_LEVEL_WARNING, module, __VA_ARGS__)
#else
#define LOG_WARNING(...) ((void)0)
#endif

#if APP_LOG_LEVEL >= APP_LOG_LEVEL_INFO
#define LOG_INFO(module, ...) \
    app_log_write(LOG_LEVEL_INFO, module, __VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if APP_LOG_LEVEL >= APP_LOG_LEVEL_DEBUG
#define LOG_DEBUG(module, ...) \
    app_log_write(LOG_LEVEL_DEBUG, module, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#endif // APP_LOG_H
