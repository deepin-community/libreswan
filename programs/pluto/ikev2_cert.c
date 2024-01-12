/* Support for IKEv2 CERT payloads, for libreswan
 *
 * Copyright (C) 2000 Andreas Hess, Patric Lichtsteiner, Roger Wegmann
 * Copyright (C) 2001 Marco Bertossa, Andreas Schleiss
 * Copyright (C) 2002 Mario Strasser
 * Copyright (C) 2000-2004 Andreas Steffen, Zuercher Hochschule Winterthur
 * Copyright (C) 2006-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2008-2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2009 Gilles Espinasse <g.esp@free.fr>
 * Copyright (C) 2012-2013 Paul Wouters <paul@libreswan.org>
 * Copyright (C) 2012 Wes Hardaker <opensource@hardakers.net>
 * Copyright (C) 2013 Matt Rogers <mrogers@redhat.com>
 * Copyright (C) 2013-2019 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2013 Kim B. Heino <b@bbbs.net>
 * Copyright (C) 2018-2022 Andrew Cagney
 * Copyright (C) 2018 Sahana Prasad <sahana.prasad07@gmail.com>
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

#include "lswnss.h"

#include "defs.h"

#include "ikev2_cert.h"
#include "state.h"
#include "connections.h"
#include "nss_cert_verify.h"
#include "ikev2_message.h"
#include "log.h"
#include "pluto_x509.h"		/* for get_auth_chain() */

/*
 * Send v2 CERT and possible CERTREQ (which should be separated
 * eventually).
 */

stf_status emit_v2CERT(const struct connection *c, struct pbs_out *outpbs)
{
	const struct cert *mycert = c->local->config->host.cert.nss_cert != NULL ? &c->local->config->host.cert : NULL;
	bool send_authcerts = c->send_ca != CA_SEND_NONE;
	bool send_full_chain = send_authcerts && c->send_ca == CA_SEND_ALL;

	if (impair.send_pkcs7_thingie) {
		llog(RC_LOG, outpbs->outs_logger, "IMPAIR: sending cert as PKCS7 blob");
		passert(mycert != NULL);
		SECItem *pkcs7 = nss_pkcs7_blob(mycert, send_full_chain);
		if (!pexpect(pkcs7 != NULL)) {
			return STF_INTERNAL_ERROR;
		}
		struct ikev2_cert pkcs7_hdr = {
			.isac_critical = build_ikev2_critical(false, outpbs->outs_logger),
			.isac_enc = CERT_PKCS7_WRAPPED_X509,
		};
		pb_stream cert_pbs;
		if (!out_struct(&pkcs7_hdr, &ikev2_certificate_desc,
				outpbs, &cert_pbs) ||
		    !out_hunk(same_secitem_as_chunk(*pkcs7), &cert_pbs, "PKCS7")) {
			SECITEM_FreeItem(pkcs7, PR_TRUE);
			return STF_INTERNAL_ERROR;
		}
		close_output_pbs(&cert_pbs);
		SECITEM_FreeItem(pkcs7, PR_TRUE);
		return STF_OK;
	}

	/*****
	 * From here on, if send_authcerts, we are obligated to:
	 * free_auth_chain(auth_chain, chain_len);
	 *****/

	chunk_t auth_chain[MAX_CA_PATH_LEN] = { { NULL, 0 } };
	int chain_len = 0;

	if (send_authcerts) {
		chain_len = get_auth_chain(auth_chain, MAX_CA_PATH_LEN,
					   mycert,
					   send_full_chain ? true : false);
	}

	const struct ikev2_cert certhdr = {
		.isac_critical = build_ikev2_critical(false, outpbs->outs_logger),
		.isac_enc = cert_ike_type(mycert),
	};

	/*   send own (Initiator CERT) */
	{
		pb_stream cert_pbs;

		dbg("sending [CERT] of certificate: %s", cert_nickname(mycert));

		if (!out_struct(&certhdr, &ikev2_certificate_desc,
				outpbs, &cert_pbs) ||
		    !out_hunk(cert_der(mycert), &cert_pbs, "CERT")) {
			free_auth_chain(auth_chain, chain_len);
			return STF_INTERNAL_ERROR;
		}

		close_output_pbs(&cert_pbs);
	}

	/* send optional chain CERTs */
	{
		for (int i = 0; i < chain_len ; i++) {
			pb_stream cert_pbs;

			dbg("sending an authcert");

			if (!out_struct(&certhdr, &ikev2_certificate_desc,
				outpbs, &cert_pbs) ||
			    !out_hunk(auth_chain[i], &cert_pbs, "CERT"))
			{
				free_auth_chain(auth_chain, chain_len);
				return STF_INTERNAL_ERROR;
			}
			close_output_pbs(&cert_pbs);
		}
	}
	free_auth_chain(auth_chain, chain_len);
	return STF_OK;
}

/*
 * For IKEv2, returns TRUE if we should be sending a cert.
 *
 * EAP-Only, for instance, never sends certs.
 */
bool ikev2_send_cert_decision(const struct ike_sa *ike)
{
	const struct connection *c = ike->sa.st_connection;
	const struct end *this = &c->spd.this;

	if (ike->sa.st_peer_wants_null) {
		/* XXX: only ever true on responder */
		dbg("IKEv2 CERT: not sending cert: peer wants (we're using?) NULL")
		return false;
	}

	if (c->local->config->host.auth == AUTH_EAPONLY) {
		dbg("IKEv2 CERT: not sending cert: local %sauth==EAPONLY",
		    c->local->config->leftright)
		return false;
	}

	if (!authby_has_digsig(c->local->config->host.authby)) {
		authby_buf pb;
		dbg("IKEv2 CERT: not sending cert: local %sauthby=%s does not have RSA or ECDSA",
		    c->local->config->leftright,
		    str_authby(c->local->config->host.authby, &pb));
		return false;
	}

	if (this->config->host.cert.nss_cert == NULL) {
		dbg("IKEv2 CERT: not sending cert: there is no local certificate to send");
		return false;
	}

	if (c->local->config->host.sendcert == CERT_SENDIFASKED &&
	    ike->sa.st_v2_ike_seen_certreq) {
		dbg("IKEv2 CERT: OK to send certificate (send if asked)");
		return true;
	}

	if (c->local->config->host.sendcert == CERT_ALWAYSSEND) {
		dbg("IKEv2 CERT: OK to send a certificate (always)");
		return true;
	}

	return false;
}
