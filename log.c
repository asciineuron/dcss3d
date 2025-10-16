#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum log_level { LOG_NONE, LOG_ERR, LOG_WARN, LOG_INFO, LOG_TRACE };

static const char *level_env_vals[] = { [LOG_NONE] = "NONE",
					[LOG_ERR] = "ERR",
					[LOG_WARN] = "WARN",
					[LOG_INFO] = "INFO",
					[LOG_TRACE] = "TRACE" };

static const char level_env_key[] = "AN_LOG_LEVEL";

enum log_level log_level = LOG_WARN;

static bool use_color = false;

void log_init(void)
{
	if (isatty(fileno(stderr))) {
		use_color = true;
	}

	char *level_env = getenv(level_env_key);
	if (!level_env)
		return;

	if (strcmp(level_env, level_env_vals[LOG_ERR]) == 0) {
		log_level = LOG_ERR;
	} else if (strcmp(level_env, level_env_vals[LOG_WARN]) == 0) {
		log_level = LOG_WARN;
	} else if (strcmp(level_env, level_env_vals[LOG_INFO]) == 0) {
		log_level = LOG_INFO;
	} else if (strcmp(level_env, level_env_vals[LOG_TRACE]) == 0) {
		log_level = LOG_TRACE;
	} else if (strcmp(level_env, level_env_vals[LOG_NONE]) == 0) {
		log_level = LOG_NONE;
	}
}

// TODO: add extra output info, e.g. date and time, and coloring

void log_err_full(const char *file, const int line, const char *fmt, ...)
{
	if (log_level < LOG_ERR)
		return;

	fprintf(stderr, "ERROR: (%s:%d) ", file, line);

	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);

	fprintf(stderr, "\n");
}

void log_warn_full(const char *file, const int line, const char *fmt, ...)
{
	if (log_level < LOG_WARN)
		return;

	fprintf(stderr, "WARNING: (%s:%d) ", file, line);

	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);

	fprintf(stderr, "\n");
}

void log_info(const char *fmt, ...)
{
	if (log_level < LOG_INFO)
		return;

	fprintf(stderr, "INFO: ");

	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);

	fprintf(stderr, "\n");
}

void log_trace(const char *fmt, ...)
{
	if (log_level < LOG_TRACE)
		return;

	fprintf(stderr, "TRACE: ");

	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);

	fprintf(stderr, "\n");
}
