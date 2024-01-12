/* log limiter, for libreswan
 *
 * Copyright (C) 2022 Andrew Cagney
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <pthread.h>    /* Must be the first include file; XXX: why? */

#include "log_limiter.h"
#include "log.h"

#include "defs.h"
#include "demux.h"

struct log_limiter {
	pthread_mutex_t mutex;
	const unsigned limit;
	volatile unsigned count;
	const char *what;
};

#define LOG_LIMIT(N, WHAT) {				\
		.mutex = PTHREAD_MUTEX_INITIALIZER,	\
		.limit = N,				\
		.what = WHAT,				\
	}

#define RATE_LIMIT 1000
struct log_limiter md_log_limiter = LOG_LIMIT(RATE_LIMIT, "message digest");
struct log_limiter certificate_log_limiter = LOG_LIMIT(10, "bad certificate");

static unsigned log_limit(const struct log_limiter *limiter)
{
	if (impair.log_rate_limit == 0) {
		/* unimpaired; use default */
		/* --impair log-rate-limit:no */
		return limiter->limit;
	} else {
		/* impaired; use value */
		/* --impair log-rate-limit:yes */
		/* --impair log-rate-limit:NNN */
		return impair.log_rate_limit;
	}
}

bool log_is_limited(struct logger *logger, struct log_limiter *limiter)
{
	/* allow imparing to override specified limit */
	unsigned limit = log_limit(limiter);

	bool is_limited;
	pthread_mutex_lock(&limiter->mutex);
	{
		if (limiter->count > limit) {
			is_limited = true;
		} else if (limiter->count == limit) {
			llog(LOG_STREAM, logger,
			     "%s rate limited log reached limit of %u entries",
			     limiter->what, limit);
			limiter->count++;
			is_limited = false;
		} else {
			limiter->count++;
			is_limited = false;
		}
	}
	pthread_mutex_unlock(&limiter->mutex);

	return is_limited;
}

VPRINTF_LIKE(3)
static void rate_log_raw(const char *prefix,
			 struct logger *logger,
			 const char *message,
			 va_list ap)
{
	JAMBUF(buf) {
		jam_string(buf, prefix);
		jam_logger_prefix(buf, logger);
		jam_va_list(buf, message, ap);
		jambuf_to_logger(buf, logger, LOG_STREAM);
	}
}

void rate_log(const struct msg_digest *md,
	      const char *message, ...)
{
	va_list ap;
	va_start(ap, message);
	if (log_is_limited(md->md_logger, &md_log_limiter)) {
		rate_log_raw(DEBUG_PREFIX, md->md_logger, message, ap);
	} else {
		rate_log_raw("", md->md_logger, message, ap);
	}
	va_end(ap);
}

static global_timer_cb reset_log_limiter;	/* type check */

static void reset_log_limiter(struct logger *logger)
{
	FOR_EACH_THING(limiter, &md_log_limiter, &certificate_log_limiter) {
		if (limiter->count > log_limit(limiter)) {
			llog(RC_LOG, logger, "%s rate limited log reset",
			     limiter->what);
		}
		limiter->count = 0;
	}
}

void init_log_limiter(void)
{
	enable_periodic_timer(EVENT_RESET_LOG_LIMITER,
			      reset_log_limiter,
			      RESET_LOG_LIMITER_FREQUENCY);
}
