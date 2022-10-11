/* timer event handling
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2001  D. Hugh Redelmeier.
 * Copyright (C) 2005-2008 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2008-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2012-2019 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2013 Matt Rogers <mrogers@redhat.com>
 * Copyright (C) 2017 Antony Antony <antony@phenome.org>
 * Copyright (C) 2017-2021 Andrew Cagney
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "sysdep.h"
#include "constants.h"
#include "defs.h"
#include "id.h"
#include "x509.h"
#include "certs.h"
#include "connections.h"	/* needs id.h */
#include "state.h"
#include "packet.h"
#include "demux.h"	/* needs packet.h */
#include "ipsec_doi.h"	/* needs demux.h and state.h */
#include "kernel.h"	/* needs connections.h */
#include "server.h"
#include "log.h"
#include "rnd.h"
#include "timer.h"
#include "whack.h"
#include "ikev1_dpd.h"
#include "ikev2.h"
#include "ikev2_redirect.h"
#include "pending.h" /* for flush_pending_by_connection */
#include "ikev1_xauth.h"
#ifdef USE_PAM_AUTH
#include "pam_auth.h"
#endif
#include "kernel.h"		/* for kernel_ops */
#include "nat_traversal.h"
#include "pluto_sd.h"
#include "ikev1_retry.h"
#include "ikev2_retry.h"
#include "pluto_stats.h"
#include "iface.h"
#include "ikev2_liveness.h"
#include "ikev2_mobike.h"
#include "ikev2_delete.h"		/* for submit_v2_delete_exchange() */

static int state_event_cmp(const void *lp, const void *rp)
{
	const struct state_event *const *le = lp;
	const struct state_event *const *re = rp;
	monotime_t l = (*le)->ev_time;
	monotime_t r = (*re)->ev_time;
	int sign = monotime_sub_sign(l, r);
	monotime_buf lb, rb;
	dbg("%s - %s = %d", str_monotime(l, &lb), str_monotime(r, &rb), sign);
	return sign;
}

void state_event_sort(const struct state_event **events, unsigned nr_events)
{
	qsort(events, nr_events, sizeof(*events), state_event_cmp);
}


struct state_event **state_event_slot(struct state *st, enum event_type type)
{
	/*
	 * Return a pointer to the event in the state object.
	 *
	 * XXX: why not just have an array and index it by KIND?
	 *
	 * Because events come in groups.  For instance, for IKEv2,
	 * only one of REPLACE / EXPIRE or REKEY / REAUTH are ever
	 * scheduled.
	 */
	switch (type) {

	case EVENT_v1_SEND_XAUTH:
		return &st->st_v1_send_xauth_event;
	case EVENT_v1_DPD:
	case EVENT_v1_DPD_TIMEOUT:
		return &st->st_v1_dpd_event;

	case EVENT_v2_LIVENESS:
		return &st->st_v2_liveness_event;

	case EVENT_v2_ADDR_CHANGE:
		return &st->st_v2_addr_change_event;

	case EVENT_v2_REKEY:
	case EVENT_v2_REAUTH:
		return &st->st_v2_refresh_event;

	case EVENT_RETRANSMIT:
		return &st->st_retransmit_event;

	case EVENT_SA_REPLACE:
	case EVENT_SA_EXPIRE:
		switch (st->st_ike_version) {
		case IKEv1: return &st->st_event;
		case IKEv2: return &st->st_v2_lifetime_event;
		}
		break;

	case EVENT_SA_DISCARD:
	case EVENT_v1_REPLACE_IF_USED:
	case EVENT_CRYPTO_TIMEOUT:
	case EVENT_v1_PAM_TIMEOUT:
		/*
		 * Many of these don't make sense - however that's
		 * what happens when (the replaced) default: is used.
		 */
		return &st->st_event;

	case EVENT_RETAIN:
	case EVENT_NULL:
		return NULL;
	}
	bad_case(type);
}

void delete_state_event(struct state_event **evp, where_t where)
{
	struct state_event *e = (*evp);
	if (e == NULL) {
		return;
	}

	passert(e->ev_state != NULL);

	dbg("#%lu deleting %s",
	    e->ev_state->st_serialno,
	    enum_name(&event_type_names, e->ev_type));

	/* first the event */
	destroy_timeout(&e->timeout);
	/* then the structure */
	dbg_free("state-event", e, where);
	pfree(e);
	*evp = NULL;

}

/*
 * This file has the event handling routines. Events are
 * kept as a linked list of event structures. These structures
 * have information like event type, expiration time and a pointer
 * to event specific data (for example, to a state structure).
 */

static void dispatch_event(struct state *st, enum event_type event_type,
			   deltatime_t event_delay);

static void timer_event_cb(void *arg, struct logger *logger)
{
	/*
	 * Start billing before state is found.
	 */
	threadtime_t inception = threadtime_start();

	/*
	 * Get rid of the old timer event before calling the timer
	 * event processor.
	 */
	struct state *st;
	enum event_type event_type;
	const char *event_name;
	deltatime_t event_delay;

	{
		struct state_event *ev = arg;
		passert(ev != NULL);
		event_type = ev->ev_type;
		event_name = enum_name(&event_type_names, event_type);
		event_delay = ev->ev_delay;
		st = ev->ev_state;	/* note: *st might be changed; XXX: why? */
		passert(st != NULL);
		passert(event_name != NULL);

		ldbg(logger, "%s: processing %s-event@%p for %s SA #%lu in state %s",
		     __func__, event_name, ev,
		     IS_IKE_SA(st) ? "IKE" : "CHILD",
		     st->st_serialno, st->st_state->short_name);

		struct state_event **evp = state_event_slot(st, event_type);
		if (evp == NULL) {
			llog_pexpect(st->st_logger, HERE,
				     "#%lu has no .st_event field for %s",
				     st->st_serialno, event_name);
			return;
		}

		if (*evp != ev) {
			llog_pexpect(st->st_logger, HERE,
				     "#%lu .st_event is %p but should be %s-pe@%p",
				     st->st_serialno, *evp, event_name, ev);
			return;
		}

		/* everything useful has been extracted */
		delete_state_event(evp, HERE);
		arg = ev = *evp = NULL; /* all gone */
	}

	statetime_t start = statetime_backdate(st, &inception);
	dispatch_event(st, event_type, event_delay);
	statetime_stop(&start, "%s() %s", __func__, event_name);
}

static void dispatch_event(struct state *st, enum event_type event_type,
			   deltatime_t event_delay)
{
	/*
	 * Check that st is as expected for the event type.
	 *
	 * For an event type associated with a state, remove the
	 * backpointer from the appropriate slot of the state object.
	 *
	 * We'll eventually either schedule a new event, or delete the
	 * state.
	 */
	switch (event_type) {

	case EVENT_v2_ADDR_CHANGE:
		dbg("#%lu IKEv2 local address change", st->st_serialno);
		ikev2_addr_change(st);
		break;

	case EVENT_RETRANSMIT:
		dbg("IKEv%d retransmit event", st->st_ike_version);
		switch (st->st_ike_version) {
		case IKEv2:
			retransmit_v2_msg(st);
			break;
#ifdef USE_IKEv1
		case IKEv1:
			retransmit_v1_msg(st);
			break;
#endif
		default:
			bad_case(st->st_ike_version);
		}
		break;

#ifdef USE_IKEv1
	case EVENT_v1_SEND_XAUTH:
		dbg("XAUTH: event EVENT_v1_SEND_XAUTH #%lu %s",
		    st->st_serialno, st->st_state->name);
		xauth_send_request(st);
		break;
#endif

	case EVENT_v2_LIVENESS:
		liveness_check(st);
		break;

	case EVENT_v2_REKEY:
		pexpect(st->st_ike_version == IKEv2);
		v2_event_sa_rekey(st);
		break;

	case EVENT_v2_REAUTH:
		pexpect(st->st_ike_version == IKEv2);
		v2_event_sa_reauth(st);
		break;

	case EVENT_SA_REPLACE:
	case EVENT_v1_REPLACE_IF_USED:
		switch (st->st_ike_version) {
		case IKEv2:
			pexpect(event_type == EVENT_SA_REPLACE);
			v2_event_sa_replace(st);
			break;
		case IKEv1:
			pexpect(event_type == EVENT_SA_REPLACE ||
				event_type == EVENT_v1_REPLACE_IF_USED);
			struct connection *c = st->st_connection;
			const char *satype = IS_IKE_SA(st) ? "IKE" : "CHILD";

			so_serial_t newer_sa = get_newer_sa_from_connection(st);
			if (newer_sa != SOS_NOBODY) {
				/* not very interesting: no need to replace */
				dbg("not replacing stale %s SA %lu; #%lu will do",
				    satype, st->st_serialno, newer_sa);
			} else if (event_type == EVENT_v1_REPLACE_IF_USED &&
				   !monobefore(mononow(), monotime_add(st->st_outbound_time, c->sa_rekey_margin))) {
				/*
				 * we observed no recent use: no need to replace
				 *
				 * The sampling effects mean that st_outbound_time
				 * could be up to SHUNT_SCAN_INTERVAL more recent
				 * than actual traffic because the sampler looks at
				 * change over that interval.
				 * st_outbound_time could also not yet reflect traffic
				 * in the last SHUNT_SCAN_INTERVAL.
				 * We expect that SHUNT_SCAN_INTERVAL is smaller than
				 * c->sa_rekey_margin so that the effects of this will
				 * be unimportant.
				 * This is just an optimization: correctness is not
				 * at stake.
				 */
				dbg("not replacing stale %s SA: inactive for %jds",
				    satype, deltasecs(monotimediff(mononow(), st->st_outbound_time)));
			} else {
				dbg("replacing stale %s SA",
				    IS_IKE_SA(st) ? "ISAKMP" : "IPsec");
				/*
				 * XXX: this call gets double billed -
				 * both to the state being deleted and
				 * to the new state being created.
				 */
				ipsecdoi_replace(st, 1);
			}

			event_delete(EVENT_v1_DPD, st);
			event_schedule(EVENT_SA_EXPIRE, st->st_replace_margin, st);
			break;
		default:
			bad_case(st->st_ike_version);
		}
		break;

	case EVENT_SA_EXPIRE:
	{
		struct connection *c = st->st_connection;
		const char *satype = IS_IKE_SA(st) ? "IKE" : "CHILD";
		so_serial_t newer_sa = get_newer_sa_from_connection(st);

		if (newer_sa != SOS_NOBODY) {
			/* not very interesting: already superseded */
			dbg("%s SA expired (superseded by #%lu)",
			    satype, newer_sa);
		} else if (!IS_IKE_SA_ESTABLISHED(st) &&
			   !IS_V1_ISAKMP_SA_ESTABLISHED(st)) {
			/* not very interesting: failed IKE attempt */
			dbg("un-established partial Child SA timeout (SA expired)");
			pstat_sa_failed(st, REASON_EXCHANGE_TIMEOUT);
		} else {
			log_state(RC_LOG, st, "%s SA expired (%s)", satype,
				  (c->policy & POLICY_DONT_REKEY) ? "--dontrekey" : "LATEST!");
		}

		/* Delete this state object.  It must be in the hash table. */
		switch (st->st_ike_version) {
		case IKEv2:
		{
			struct ike_sa *ike = ike_sa(st, HERE);
			if (ike == NULL) {
				/*
				 * XXX: SNAFU with IKE SA replacing
				 * itself (but not deleting its
				 * children?)  simultaneous to a CHILD
				 * SA failing to establish and
				 * attempting to delete / replace
				 * itself?
				 *
				 * Because these things are
				 * not serialized it is hard
				 * to say.
				 */
				log_state(RC_LOG_SERIOUS, st, "Child SA lost its IKE SA #%lu",
					  st->st_clonedfrom);
				delete_state(st);
				st = NULL;
			} else if (IS_IKE_SA(st)) {
				/* IKEv2 parent, delete children too */
				dbg("IKEv2 SA expired, delete whole family");
				passert(&ike->sa == st);
				delete_ike_family(&ike, PROBABLY_SEND_DELETE);
				/* note: no md->st to clear */
				st = NULL;
			} else if (IS_IKE_SA_ESTABLISHED(&ike->sa)) {
				/* note: no md->st to clear */
				submit_v2_delete_exchange(ike, pexpect_child_sa(st));
				st = NULL;
			} else {
				delete_state(st);
				st = NULL;
			}
			break;
		}
		case IKEv1:
			delete_state(st);
			/* note: no md->st to clear */
			/* st = NULL; */
			break;
		default:
			bad_case(st->st_ike_version);
		}
		break;
	}

	case EVENT_SA_DISCARD:
		/*
		 * The state failed to complete within a reasonable
		 * time, or the state failed but was left to live for
		 * a while so re-transmits could work, or the state is
		 * being garbage collected.  Either way, time to
		 * delete it.
		 */
		if (deltatime_cmp(event_delay, >, deltatime_zero)) {
			/* Don't bother logging 0 delay */
			deltatime_buf dtb;
			log_state(RC_LOG, st, "deleting incomplete state after %s seconds",
				  str_deltatime(event_delay, &dtb));
		}
		/*
		 * If no other reason has been given then this is a
		 * timeout.
		 */
		pstat_sa_failed(st, REASON_EXCHANGE_TIMEOUT);
		/*
		 * XXX: this is scary overkill - delete_state() likes
		 * to resurect things and/or send messages.  What's
		 * needed is a lower-level discard_state() that just
		 * does its job.
		 *
		 * XXX: for IKEv2, it looks like delete_state() will
		 * stop spontaneously sending messages (and hopefully
		 * spontaneously deleting IKE families).
		 */
		delete_state(st);
		break;

#ifdef USE_IKEv1
	case EVENT_v1_DPD:
		event_v1_dpd(st);
		break;

	case EVENT_v1_DPD_TIMEOUT:
		event_v1_dpd_timeout(st);
		break;

#ifdef USE_PAM_AUTH
	case EVENT_v1_PAM_TIMEOUT:
		dbg("PAM thread timeout on state #%lu", st->st_serialno);
		pam_auth_abort(st, "timeout");
		/*
		 * Things get cleaned up when the PAM process exits.
		 *
		 * Should this schedule an event for the case when the
		 * child process (which is SIGKILLed) doesn't exit!?!
		 */
		break;
#endif
#endif
	case EVENT_CRYPTO_TIMEOUT:
		dbg("event crypto_failed on state #%lu, aborting",
		    st->st_serialno);
		pstat_sa_failed(st, REASON_CRYPTO_TIMEOUT);
		delete_state(st);
		/* note: no md->st to clear */
		break;


	default:
		bad_case(event_type);
	}
}

/*
 * Delete all of the lifetime events (if any).
 *
 * Most lifetime events (things that kill the state) try to share a
 * single .st_event.  However, there has been and likely will be
 * exceptions (for instance the retransmit timer), and the code below
 * is written to deal with it.
 *
 * XXX:
 *
 * The decision to have all the loosely lifetime related timers
 * (retransmit, rekey, replace, ...) share a single .st_event field is
 * ...  unfortunate.  The code has to constantly juggle the field
 * deciding which event is next.  Far easier to set and forget each
 * independently.  This is why the retransmit timer has been split
 * off.
 */

void delete_event(struct state *st)
{
	delete_state_event(&st->st_event, HERE);
}

/*
 * This routine schedules a state event.
 */
void event_schedule_where(enum event_type type, deltatime_t delay, struct state *st, where_t where)
{
	passert(st != NULL);
	/*
	 * Scheduling a month into the future is most likely a bug.
	 * pexpect() causes us to flag this in our test cases
	 */
	pexpect(deltasecs(delay) < secs_per_day * 31);

	const char *event_name = enum_name(&event_type_names, type);

	struct state_event **evp = state_event_slot(st, type);
	if (evp == NULL) {
		llog_pexpect(st->st_logger, where,
			     "#%lu has no .st_*event field for %s",
			     st->st_serialno, event_name);
		return;
	}

	if (*evp != NULL) {
		/* help debugging by stumbling on */
		llog_pexpect(st->st_logger, where,
			     "#%lu already has %s scheduled; forcing %s",
			     st->st_serialno,
			     enum_name(&event_type_names, (*evp)->ev_type),
			     event_name);
		delete_state_event(evp, where);
	}

	struct state_event *ev = alloc_thing(struct state_event, event_name);
	ev->ev_type = type;
	ev->ev_state = st;
	ev->ev_epoch = mononow();
	ev->ev_delay = delay;
	ev->ev_time = monotime_add(ev->ev_epoch, delay);
	*evp = ev;

	deltatime_buf buf;
	dbg("%s: newref %s-pe@%p timeout in %s seconds for #%lu",
	    __func__, event_name, ev, str_deltatime(delay, &buf),
	    ev->ev_state->st_serialno);

	schedule_timeout(event_name, &ev->timeout, delay, timer_event_cb, ev);
}

/*
 * Delete a state backlinked event (if any); leave *evp == NULL.
 */
void event_delete_where(enum event_type type, struct state *st, where_t where)
{
	struct state_event **evp = state_event_slot(st, type);
	if (evp == NULL) {
		llog_pexpect(st->st_logger, where,
			     "#%lu has no .st_event field for %s",
			     st->st_serialno, enum_name(&event_type_names, type));
		return;
	}
	if (*evp != NULL) {
		dbg("#%lu requesting %s-event@%p be deleted "PRI_WHERE,
		    st->st_serialno, enum_name(&event_type_names, (*evp)->ev_type),
		    *evp, pri_where(where));
		pexpect(st == (*evp)->ev_state);
		delete_state_event(evp, where);
		pexpect((*evp) == NULL);
	};
}

void event_force(enum event_type type, struct state *st)
{
	event_delete(type, st);
	deltatime_t delay = deltatime(0);
	event_schedule(type, delay, st);
}

void call_state_event_handler(struct logger *logger, struct state *st,
			      enum event_type event_type)
{
	const char *event_name = enum_name_short(&event_type_names, event_type);
	if (event_name == NULL) {
		llog(RC_COMMENT, logger, "%d is not a valid event", event_type);
		return;
	}

	/* sanity checks */
	struct state_event **evp = state_event_slot(st, event_type);
	if (evp == NULL) {
		llog(RC_COMMENT, logger, "%s is not a valid event", event_name);
		return;
	}

	/*
	 * Like timer_event_cb(), delete the old event before calling
	 * the event handler.
	 */
	deltatime_t event_delay = deltatime(1);
	if (*evp == NULL) {
		llog(RC_COMMENT, logger,
		     "no existing %s event to delete", event_name);
	} else if ((*evp)->ev_type != event_type) {
		llog(RC_COMMENT, logger,
		     "deleting existing %s event occupying the slot shared with %s",
		     enum_name(&event_type_names, (*evp)->ev_type),
		     event_name);
		delete_state_event(evp, HERE);
	} else {
		llog(RC_COMMENT, logger,
		     "deleting existing %s event",
		     event_name);
		event_delay = (*evp)->ev_delay;
		delete_state_event(evp, HERE);
	}

	llog(RC_COMMENT, logger, "calling %s event handler", event_name);
	dispatch_event(st, event_type, event_delay);
}
