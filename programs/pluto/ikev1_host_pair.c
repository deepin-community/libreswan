/* information about connections between hosts
 *
 * Copyright (C) 1998-2002,2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2007 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2007 Ken Bantoft <ken@xelerance.com>
 * Copyright (C) 2008-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2010 Tuomo Soini <tis@foobar.fi>
 * Copyright (C) 2011 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2013-2019 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
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

#include "host_pair.h"
#include "ikev1_host_pair.h"
#include "log.h"
#include "connections.h"
#include "demux.h"
#include "iface.h"
#include "ikev1_spdb.h"

static bool match_v1_connection(struct connection *c, struct authby authby,
				bool policy_xauth, bool policy_aggressive,
				const struct id *peer_id)
{
	if (c->config->ike_version != IKEv1) {
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", wrong IKE version",
		    pri_connection(c, &cb));
		return false;
	}

	if (c->kind == CK_INSTANCE && c->remote->host.id.kind == ID_NULL) {
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", ID_NULL instance",
		    pri_connection(c, &cb));
		return false;
	}

	if (NEVER_NEGOTIATE(c->policy)) {
		/* are we a block or clear connection? */
		enum shunt_policy shunt = c->config->prospective_shunt;
		if (shunt != SHUNT_TRAP) {
			/*
			 * We need to match block/clear so we can send
			 * back NO_PROPOSAL_CHOSEN, otherwise not
			 * match so we can hit packetdefault to do
			 * real IKE.  clear and block do not have
			 * POLICY_OPPORTUNISTIC, but clear-or-private
			 * and private-or-clear do, but they don't do
			 * IKE themselves but allow packetdefault to
			 * be hit and do the work.  if not policy_oppo
			 * -> we hit clear/block so this is right c
			 */
			if ((c->policy & POLICY_OPPORTUNISTIC)) {
				connection_buf cb;
				dbg("  skipping "PRI_CONNECTION", never negotiate + opportunistic",
				    pri_connection(c, &cb));
				return false;
			}

			/* shunt match - stop the search for another conn if we are groupinstance*/
			if (c->policy & POLICY_GROUPINSTANCE) {
				connection_buf cb;
				dbg("  skipping "PRI_CONNECTION", never negotiate + group instance",
				    pri_connection(c, &cb));
				return true;
			}
		}
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", never negotiate",
		    pri_connection(c, &cb));
		return false;
	}

	/*
	 * Success may require exact match of:
	 * (1) XAUTH (POLICY_XAUTH)
	 * (2) kind of IKEV1 (POLICY_AGGRESSIVE)
	 * (3) IKE_VERSION
	 * So if any bits are on in the exclusive OR, we fail.
	 * Each of our callers knows what is known so specifies
	 * the policy_exact_mask.
	 */
	if (policy_xauth == !(c->policy & POLICY_XAUTH)) {
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", exact match POLICY_XAUTH failed",
		    pri_connection(c, &cb));
		return false;
	}
	if (policy_aggressive == !(c->policy & POLICY_AGGRESSIVE)) {
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", exact match POLICY_AGGRESSIVE failed",
		    pri_connection(c, &cb));
		return false;
	}

	if (peer_id != NULL && !same_id(peer_id, &c->remote->host.id) &&
	    (c->remote->host.id.kind != ID_FROMCERT && !id_is_any(&c->remote->host.id))) {
		connection_buf cb;
		dbg("  skipping "PRI_CONNECTION", peer_id failed",
		    pri_connection(c, &cb));
		return false; /* incompatible ID */
	}

	/*
	 * Check that the proposed authby matches the connection's
	 * auth (IKEv1 only does one auth per connection) so match
	 * needs to be exact.
	 *
	 * Order matters.  First match, be it RSA or PSK is accepted.
	 */
	switch (c->remote->config->host.auth) {
	case AUTH_RSASIG:
		if (!authby.rsasig) {
			connection_buf cb;
			dbg("  skipping "PRI_CONNECTION", RSASIG was not proposed",
			    pri_connection(c, &cb));
			return false;
		}
		break;
	case AUTH_PSK:
		if (!authby.psk) {
			connection_buf cb;
			dbg("  skipping "PRI_CONNECTION", PSK was not proposed",
			    pri_connection(c, &cb));
			return false;
		}
		break;
	default:
	{
		connection_buf cb;
		enum_buf eb;
		dbg("  skipping "PRI_CONNECTION", %s is never proposed",
		    pri_connection(c, &cb),
		    str_enum(&keyword_auth_names, c->remote->config->host.auth, &eb));
		return false;
	}
	}

	return true;
}

/*
 * find_host_connection: find the first satisfactory connection
 *	with this pair of hosts.
 *
 * find_next_host_connection: find the next satisfactory connection
 *	Starts where find_host_connection left off.
 *	NOTE: it will return its argument; if you want to
 *	advance, use c->hp_next.
 *
 * We start with the list that find_host_pair_connections would yield
 * but we narrow the selection.
 *
 * We only yield a connection that can negotiate.
 *
 * The caller can specify policy requirements as
 * req_policy and policy_exact_mask.
 *
 * All policy bits found in req_policy must be in the
 * policy of the connection.
 *
 * For all bits in policy_exact mask, the req_policy
 * and connection's policy must be equal.  Likely candidates:
 * - XAUTH (POLICY_XAUTH)
 * - kind of IKEV1 (POLICY_AGGRESSIVE)
 * These should only be used if the caller actually knows
 * the exact value and has included it in req_policy.
 */

static struct connection *find_v1_host_connection(const ip_address local_address,
						  const ip_address remote_address,
						  struct authby authby, bool policy_xauth,
						  bool policy_aggressive,
						  const struct id *peer_id)
{
	address_buf lb;
	address_buf rb;
	authby_buf pb;
	dbg("%s() %s->%s authby=%s xauth=%s aggressive=%s but ignoring ports",
	    __func__,
	    str_address(&remote_address, &rb),
	    str_address(&local_address, &lb),
	    str_authby(authby, &pb),
	    bool_str(policy_xauth),
	    bool_str(policy_aggressive));

	struct connection *c = NULL;
	FOR_EACH_HOST_PAIR_CONNECTION(local_address, remote_address, d) {
		if (!match_v1_connection(d, authby, policy_xauth,
					 policy_aggressive, peer_id)) {
			continue;
		}

		/*
		 * This could be a shared ISAKMP SA connection, in
		 * which case we prefer to find the connection that
		 * has the ISAKMP SA.
		 */
		if (d->newest_ike_sa != SOS_NOBODY) {
			/* instant winner */
			c = d;
			break;
		}
		if (c == NULL) {
			c = d;
		}
	}

	return c;
}

struct connection *find_v1_aggr_mode_connection(struct msg_digest *md,
						struct authby authby,
						bool policy_xauth,
						const struct id *peer_id)
{
	struct connection *c;

	c = find_v1_host_connection(md->iface->ip_dev->id_address,
				    endpoint_address(md->sender),
				    authby, policy_xauth,
				    true/*POLICY_AGGRESSIVE*/,
				    peer_id);
	if (c != NULL) {
		return c;
	}

	c = find_v1_host_connection(md->iface->ip_dev->id_address, unset_address,
				    authby, policy_xauth,
				    true/*POLICY_AGGRESSIVE*/,
				    peer_id);
	if (c != NULL) {
		/* Create a temporary connection that is a copy of this one.
		 * Peers ID isn't declared yet.
		 */
		ip_address sender_address = endpoint_address(md->sender);
		return rw_instantiate(c, &sender_address, NULL, NULL);
	}

	endpoint_buf b;
	authby_buf pb;
	llog(RC_LOG_SERIOUS, md->md_logger,
	     "initial Aggressive Mode message from %s but no (wildcard) connection has been configured with authby %s",
	     str_endpoint(&md->sender, &b),
	     str_authby(authby, &pb));

	return NULL;
}

struct connection *find_v1_main_mode_connection(struct msg_digest *md)
{
	struct connection *c;

	/* random source ports are handled by find_host_connection */

	struct payload_digest *const sa_pd = md->chain[ISAKMP_NEXT_SA];
	struct authby authby = {0};
	bool policy_xauth = false;
	diag_t d = preparse_isakmp_sa_body(sa_pd->pbs, &authby, &policy_xauth);
	if (d != NULL) {
		llog_diag(RC_LOG_SERIOUS, md->md_logger, &d,
			  "initial Main Mode message has corrupt SA payload: ");
		return NULL;
	}

	/*
	 * This call does not try to match authentication
	 * (preparse_isakmp_sa_body() isn't called).  Hence LEMPTY fir
	 * policy and FALSE for exact_match_POLICY_XAUTH - neither of
	 * these are known.
	 *
	 * Why?
	 */

	c = find_v1_host_connection(md->iface->ip_dev->id_address,
				    endpoint_address(md->sender),
				    authby, policy_xauth,
				    false/*POLICY_AGGRESSIVE*/,
				    NULL /* peer ID not known yet */);
	if (c != NULL) {
		/*
		 * we found a non %any conn. double check if it needs
		 * instantiation anyway (eg vnet=)
		 */
		if (c->kind == CK_TEMPLATE) {
			ldbg(md->md_logger,
			     "local endpoint needs instantiation");
			ip_address sender_address = endpoint_address(md->sender);
			return rw_instantiate(c, &sender_address, NULL, NULL);
		}

		return c;
	}

	/*
	 * Other IKE clients, such as strongswan, send the XAUTH VID
	 * even for connections they do not want to run XAUTH on.  We
	 * need to depend on the policy negotiation, not the VID.  So
	 * we ignore md->quirks.xauth_vid
	 */

	/*
	 * See if a wildcarded connection can be found.  We cannot
	 * pick the right connection, so we're making a guess.  All
	 * Road Warrior connections are fair game: we pick the first
	 * we come across (if any).  If we don't find any, we pick the
	 * first opportunistic with the smallest subnet that includes
	 * the peer.  There is, of course, no necessary relationship
	 * between an Initiator's address and that of its client, but
	 * Food Groups kind of assumes one.
	 */

	FOR_EACH_HOST_PAIR_CONNECTION(md->iface->ip_dev->id_address, unset_address, d) {

		if (!match_v1_connection(d, authby, policy_xauth,
					 false/*POLICY_AGGRESSIVE*/,
					 NULL /* peer ID not known yet */)) {
			continue;
		}

		if (d->kind == CK_GROUP) {
			continue;
		}

		if (d->kind == CK_TEMPLATE) {
			/*
			 * must be Road Warrior: we have a
			 * winner
			 */
			c = d;
			break;
		}

		/*
		 * Opportunistic or Shunt:
		 * pick tightest match
		 */
		if (endpoint_in_selector(md->sender, d->spd.that.client) &&
		    (c == NULL || selector_in_selector(c->spd.that.client,
						       d->spd.that.client))) {
			c = d;
		}
	}

	if (c == NULL) {
		authby_buf ab;
		llog(RC_LOG_SERIOUS, md->md_logger,
		     "initial Main Mode message received but no connection has been authorized with authby=%s and xauth=%s",
		     str_authby(authby, &ab), bool_str(policy_xauth));
		/* XXX notification is in order! */
		return NULL;
	}

	if (c->kind != CK_TEMPLATE) {
		connection_buf cib;
		llog(RC_LOG_SERIOUS, md->md_logger,
		     "initial Main Mode message received but "PRI_CONNECTION" forbids connection",
		     pri_connection(c, &cib));
		/* XXX notification is in order! */
		return NULL;
	}

	/*
	 * Create a temporary connection that is a copy of this one.
	 *
	 * Their ID isn't declared yet.
	 */
	connection_buf cib;
	ldbg(md->md_logger, "instantiating "PRI_CONNECTION" for initial Main Mode message",
	     pri_connection(c, &cib));
	ip_address sender_address = endpoint_address(md->sender);
	return rw_instantiate(c, &sender_address, NULL, NULL);
}
