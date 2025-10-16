#ifndef LOG_H
#define LOG_H

void log_err_full(const char *file, const int line, const char *fmt, ...);

#define log_err(fmt, ...)                                     \
	do {                                                  \
		log_err_full(__FILE__, __LINE__,              \
			     fmt __VA_OPT__(, ) __VA_ARGS__); \
	} while (0)

void log_warn_full(const char *file, const int line, const char *fmt, ...);

#define log_warn(fmt, ...)                                     \
	do {                                                   \
		log_warn_full(__FILE__, __LINE__,              \
			      fmt __VA_OPT__(, ) __VA_ARGS__); \
	} while (0)

void log_info(const char *fmt, ...);

void log_trace(const char *fmt, ...);

void log_init(void);

#endif
