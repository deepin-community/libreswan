/* rekey connections: IKEv2
 *
 * Copyright (C) 1998-2002,2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2008 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2009 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2013-2019 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2019 Antony Antony <antony@phenome.org>
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

#include "defs.h"
#include "log.h"
#include "connections.h"
#include "state.h"
#include "timer.h"

struct rekey_how {
	bool background;
	enum sa_type sa_type;
};

static void rekey_state(struct state *st, bool background, struct logger *logger)
{
	if (!background) {
		/* XXX: something better? */
		fd_delref(&st->st_logger->object_whackfd);
		st->st_logger->global_whackfd = fd_addref(logger->global_whackfd);
	}
	event_force(EVENT_v2_REKEY, st);
}

static int rekey_connection_now(struct connection *c,
				void *arg, struct logger *logger)
{
	if (c->config->ike_version != IKEv2) {
		llog(RC_LOG, logger, "cannot force rekey of %s connection",
		     c->config->ike_info->version_name);
		return 0;
	}
	struct rekey_how *how = arg;
	struct state *st;
	switch (how->sa_type) {
	case IKE_SA:
		st = state_by_serialno(c->newest_ike_sa);
		break;
	case IPSEC_SA:
		st = state_by_serialno(c->newest_ipsec_sa);
		break;
	default:
		bad_case(how->sa_type);
	}
	if (st == NULL) {
		llog(RC_LOG, logger, "connection does not have %s",
		     c->config->ike_info->sa_type_name[how->sa_type]);
		return 0;
	}
	rekey_state(st, how->background, logger);
	return 1;
}

void rekey_now(const char *str, enum sa_type sa_type,
	       bool background, struct logger *logger)
{

	/* see if we got a stat enumber or name */
	char *err = NULL;
	int num = strtol(str, &err, 0);

	if (str == err || *err != '\0') {

		struct rekey_how how = {
			.background = background,
			.sa_type = sa_type,
		};

		/*
		 * Loop because more than one may match (template and
		 * instances) but only interested in instances.  Don't
		 * log an error if not found before we checked
		 * aliases.
		 *
		 * connection instances may need more work to work ???
		 */

		if (foreach_concrete_connection_by_name(str, rekey_connection_now,
							&how, logger) >= 0) {
			/* logged by rekey_connection_now() */
			dbg("found connections by name");
			return;
		}

		int count = foreach_connection_by_alias(str, rekey_connection_now,
							&how, logger);
		if (count == 0) {
			llog(RC_UNKNOWN_NAME, logger,
			     "no such connection or aliased connection named \"%s\"", str);
		} else {
			llog(RC_COMMENT, logger,
			     "rekeyed %d connections from aliased connection \"%s\"",
			     count, str);
		}
	} else {
		/* str is a state number - this overrides ike vs ipsec rekey command */
		struct state *st = state_by_serialno(num);
		if (st == NULL) {
			llog(RC_LOG, logger, "can't find SA #%d to rekey", num);
			return;
		}

		struct connection *c = st->st_connection;
		if (IS_IKE_SA(st)) {
			connection_buf cb;
			llog(RC_LOG, logger, "rekeying IKE SA state #%d of connection "PRI_CONNECTION"",
			     num, pri_connection(c, &cb));
			rekey_state(st, background, logger);
		} else {
			connection_buf cb;
			llog(RC_LOG, logger, "rekeying IPsec SA state #%d of connection "PRI_CONNECTION"",
			     num, pri_connection(c, &cb));
			rekey_state(st, background, logger);
		}
	}
}
