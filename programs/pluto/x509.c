/* Support of X.509 certificates and CRLs for libreswan
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
 * Copyright (C) 2018-2019 Andrew Cagney <cagney@gnu.org>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>

#include "sysdep.h"
#include "lswconf.h"
#include "lswnss.h"
#include "constants.h"

#include "defs.h"
#include "log.h"
#include "id.h"
#include "asn1.h"
#include "packet.h"
#include "demux.h"
#include "ipsec_doi.h"
#include "oid.h"
#include "x509.h"
#include "certs.h"
#include "keys.h"
#include "packet.h"
#include "demux.h"      /* needs packet.h */
#include "connections.h"
#include "state.h"
#include "whack.h"
#include "fetch.h"
#include "host_pair.h" 		/* for FOR_EACH_HOST_PAIR_CONNECTION() */
#include "secrets.h"
#include "ip_address.h"
#include "ikev2_message.h"	/* for build_ikev2_critical() */
#include "ike_alg_hash.h"
#include "certs.h"
#include "root_certs.h"
#include "iface.h"

/* new NSS code */
#include "pluto_x509.h"
#include "nss_cert_load.h"
#include "nss_cert_verify.h"

/* NSS */
#include <prtime.h>
#include <keyhi.h>
#include <cert.h>
#include <certdb.h>
#include <secoid.h>
#include <secerr.h>
#include <secder.h>
#include <ocsp.h>
#include "crypt_hash.h"
#include "crl_queue.h"
#include "ip_info.h"

bool crl_strict = false;
bool ocsp_strict = false;
bool ocsp_enable = false;
bool ocsp_post = false;
char *curl_iface = NULL;
long curl_timeout = -1;

SECItem same_shunk_as_dercert_secitem(shunk_t shunk)
{
	return same_shunk_as_secitem(shunk, siDERCertBuffer);
}

static const char *dntoasi(dn_buf *dst, SECItem si)
{
	return str_dn(same_secitem_as_shunk(si), dst);
}

/*
 * does our CA match one of the requested CAs?
 */
bool match_requested_ca(const generalName_t *requested_ca, chunk_t our_ca,
			int *our_pathlen)
{
	/* if no ca is requested than any ca will match */
	if (requested_ca == NULL) {
		*our_pathlen = 0;
		return true;
	}

	*our_pathlen = MAX_CA_PATH_LEN + 1;

	while (requested_ca != NULL) {
		int pathlen;

		if (trusted_ca(ASN1(our_ca), ASN1(requested_ca->name), &pathlen) &&
		    pathlen < *our_pathlen)
			*our_pathlen = pathlen;
		requested_ca = requested_ca->next;
	}

	return *our_pathlen <= MAX_CA_PATH_LEN;
}

static void same_nss_gn_as_pluto_gn(CERTGeneralName *nss_gn,
				    generalName_t *pluto_gn)
{
	switch (nss_gn->type) {
	case certOtherName:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.OthName.name);
		pluto_gn->kind = GN_OTHER_NAME;
		break;

	case certRFC822Name:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_RFC822_NAME;
		break;

	case certDNSName:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_DNS_NAME;
		break;

	case certX400Address:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_X400_ADDRESS;
		break;

	case certEDIPartyName:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_EDI_PARTY_NAME;
		break;

	case certURI:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_URI;
		break;

	case certIPAddress:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_IP_ADDRESS;
		break;

	case certRegisterID:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->name.other);
		pluto_gn->kind = GN_REGISTERED_ID;
		break;

	case certDirectoryName:
		pluto_gn->name = same_secitem_as_chunk(nss_gn->derDirectoryName);
		pluto_gn->kind = GN_DIRECTORY_NAME;
		break;

	default:
		bad_case(nss_gn->type);
	}
}

/*
 * Checks if CA a is trusted by CA b
 * This very well could end up being condensed into
 * an NSS call or two. TBD.
 */
bool trusted_ca(asn1_t a, asn1_t b, int *pathlen)
{
	if (DBGP(DBG_BASE)) {
		if (a.ptr != NULL) {
			dn_buf abuf;
	    		DBG_log("%s: trustee A = '%s'", __func__,
				str_dn(a, &abuf));
		}
		if (b.ptr != NULL) {
			dn_buf bbuf;
	    		DBG_log("%s: trustor B = '%s'", __func__,
				str_dn(b, &bbuf));
		}
	}

	/* no CA b specified => any CA a is accepted */
	if (b.ptr == NULL) {
		*pathlen = (a.ptr == NULL) ? 0 : MAX_CA_PATH_LEN;
		return true;
	}

	/* no CA a specified => trust cannot be established */
	if (a.ptr == NULL) {
		*pathlen = MAX_CA_PATH_LEN;
		return false;
	}

	*pathlen = 0;

	/* CA a equals CA b => we have a match */
	if (same_dn(a, b)) {
		return true;
	}

	/*
	 * CERT_GetDefaultCertDB() simply returns the contents of a
	 * static variable set by NSS_Initialize().  It doesn't check
	 * the value and doesn't set PR error.  Short of calling
	 * CERT_SetDefaultCertDB(NULL), the value can never be NULL.
	 */
	CERTCertDBHandle *handle = CERT_GetDefaultCertDB();
	passert(handle != NULL);

	/* CA a might be a subordinate CA of b */

	bool match = false;
	CERTCertificate *cacert = NULL;

	while ((*pathlen)++ < MAX_CA_PATH_LEN) {
		SECItem a_dn = same_shunk_as_dercert_secitem(a);
		cacert = CERT_FindCertByName(handle, &a_dn);

		/* cacert not found or self-signed root cacert => exit */
		if (cacert == NULL || CERT_IsRootDERCert(&cacert->derCert)) {
			break;
		}

		/* does the issuer of CA a match CA b? */
		asn1_t i_dn = same_secitem_as_shunk(cacert->derIssuer);
		match = same_dn(i_dn, b);

		if (match) {
			/* we have a match: exit the loop */
			dbg("%s: A is a subordinate of B", __func__);
			break;
		}

		/* go one level up in the CA chain */
		a = i_dn;
		CERT_DestroyCertificate(cacert);
		cacert = NULL;
	}

	dbg("%s: returning %s at pathlen %d",
	    __func__, match ? "trusted" : "untrusted", *pathlen);

	if (cacert != NULL) {
		CERT_DestroyCertificate(cacert);
	}
	return match;
}

/*
 * choose either subject DN or a subjectAltName as connection end ID
 */
void select_nss_cert_id(CERTCertificate *cert, struct id *end_id)
{
	if (end_id->kind == ID_FROMCERT) {
		dbg("setting ID to ID_DER_ASN1_DN: \'%s\'", cert->subjectName);
		chunk_t name = clone_secitem_as_chunk(cert->derSubject, "cert id");
		end_id->name = ASN1(name);
		end_id->scratch = name.ptr;
		end_id->kind = ID_DER_ASN1_DN;
	}
}

generalName_t *collect_rw_ca_candidates(struct msg_digest *md)
{
	generalName_t *top = NULL;
	FOR_EACH_HOST_PAIR_CONNECTION(md->iface->ip_dev->id_address, unset_address, d) {

#if 0
		/* REMOTE==%any so d can never be an instance */
		if (d->kind == CK_INSTANCE && d->remote->host.id.kind == ID_NULL) {
			connection_buf cb;
			dbg("skipping unauthenticated "PRI_CONNECTION" with ID_NULL",
			    pri_connection(d, &cb));
			continue;
		}
#endif

		if (NEVER_NEGOTIATE(d->policy)) {
			continue;
		}

		/* we require a road warrior connection */
		if (d->kind != CK_TEMPLATE ||
		    (d->policy & POLICY_OPPORTUNISTIC) ||
		    d->remote->config->host.ca.ptr == NULL) {
			continue;
		}

		for (generalName_t *gn = top; ; gn = gn->next) {
			if (gn == NULL) {
				/* prepend a new gn for D */
				gn = alloc_thing(generalName_t, "generalName");
				gn->kind = GN_DIRECTORY_NAME;
				gn->name = d->remote->config->host.ca;
				gn->next = top;
				top = gn;
				break;
			}
			if (same_dn(ASN1(gn->name), ASN1(d->remote->config->host.ca))) {
				/* D's CA already in list */
				break;
			}
		}
	}
	return top;
}

/*
 *  Converts a X.500 generalName into an ID
 */
static void gntoid(struct id *id, const generalName_t *gn, struct logger *logger)
{
	*id = empty_id;

	switch (gn->kind) {
	case GN_DNS_NAME:	/* ID type: ID_FQDN */
		id->kind = ID_FQDN;
		id->name = ASN1(gn->name);
		break;
	case GN_IP_ADDRESS:	/* ID type: ID_IPV4_ADDR */
	{
		/*
		 * XXX: why could this fail; and what happens when it
		 * is ignored?
		 */
		const struct ip_info *afi = &ipv4_info;
		id->kind = afi->id_ip_addr;
		err_t ugh = hunk_to_address(gn->name, afi, &id->ip_addr);
		if (ugh != NULL) {
			llog(RC_LOG, logger,
				    "warning: gntoid() failed to initaddr(): %s",
				    ugh);
		}
		break;
	}
	case GN_RFC822_NAME:	/* ID type: ID_USER_FQDN */
		id->kind = ID_USER_FQDN;
		id->name = ASN1(gn->name);
		break;
	case GN_DIRECTORY_NAME:
		id->kind = ID_DER_ASN1_DN;
		id->name = ASN1(gn->name);
		break;
	default:
		id->kind = ID_NONE;
		id->name = null_shunk;
		break;
	}
}

/*
 * Convert all CERTCertificate general names to a list of pluto generalName_t
 * Results go in *gn_out.
 */
static void get_pluto_gn_from_nss_cert(CERTCertificate *cert, generalName_t **gn_out, PRArenaPool *arena)
{
	generalName_t *pgn_list = NULL;
	CERTGeneralName *first_nss_gn = CERT_GetCertificateNames(cert, arena);

	if (first_nss_gn != NULL) {
		CERTGeneralName *cur_nss_gn = first_nss_gn;

		do {
			generalName_t *pluto_gn =
				alloc_thing(generalName_t,
					    "get_pluto_gn_from_nss_cert: converted gn");
			dbg("%s: allocated pluto_gn %p", __func__, pluto_gn);
			same_nss_gn_as_pluto_gn(cur_nss_gn, pluto_gn);
			pluto_gn->next = pgn_list;
			pgn_list = pluto_gn;
			/*
			 * CERT_GetNextGeneralName just loops around, does not end at NULL.
			 */
			cur_nss_gn = CERT_GetNextGeneralName(cur_nss_gn);
		} while (cur_nss_gn != first_nss_gn);
	}

	*gn_out = pgn_list;
}

static diag_t create_cert_subjectdn_pubkey(CERTCertificate *cert,
					   struct pubkey **pk,
					   struct logger *logger)
{
	struct id id = {
		.kind = ID_DER_ASN1_DN,
		.name = same_secitem_as_shunk(cert->derSubject),
	};
	return create_pubkey_from_cert(&id, cert, pk, logger);
}

static void add_cert_san_pubkeys(struct pubkey_list **pubkey_db,
				 CERTCertificate *cert,
				 struct logger *logger)
{
	PRArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);

	generalName_t *gnt;
	get_pluto_gn_from_nss_cert(cert, &gnt, arena);

	for (generalName_t *gn = gnt; gn != NULL; gn = gn->next) {
		struct id id;

		gntoid(&id, gn, logger);
		if (id.kind != ID_NONE) {
			struct pubkey *pk = NULL;
			diag_t d = create_pubkey_from_cert(&id, cert, &pk, logger);
			if (d != NULL) {
				llog_diag(RC_LOG, logger, &d, "%s", "");
				passert(pk == NULL);
				return;
			}
			replace_public_key(pubkey_db, &pk/*stolen*/);
		}
	}

	free_generalNames(gnt, false);
	if (arena != NULL) {
		PORT_FreeArena(arena, PR_FALSE);
	}
}

/*
 * Adds pubkey entries from a certificate.
 * An entry with the ID_DER_ASN1_DN subject is always added
 * with subjectAltNames
 * @keyid provides an id for a secondary entry
 */
bool add_pubkey_from_nss_cert(struct pubkey_list **pubkey_db,
			      const struct id *keyid, CERTCertificate *cert,
			      struct logger *logger)
{
	struct pubkey *pk = NULL;
	diag_t d = create_cert_subjectdn_pubkey(cert, &pk, logger);
	if (d != NULL) {
		llog_diag(RC_LOG, logger, &d, "%s", "");
		dbg("failed to create subjectdn_pubkey from cert");
		return false;
	}

	replace_public_key(pubkey_db, &pk);
	passert(pk == NULL); /*stolen*/

	add_cert_san_pubkeys(pubkey_db, cert, logger);

	if (keyid != NULL && keyid->kind != ID_DER_ASN1_DN &&
			     keyid->kind != ID_NONE &&
			     keyid->kind != ID_FROMCERT) {
		struct pubkey *pk2 = NULL;
		diag_t d = create_pubkey_from_cert(keyid, cert, &pk2, logger);
		if (d != NULL) {
			llog_diag(RC_LOG, logger, &d, "%s", "");
			/* ignore? */
		} else {
			replace_public_key(pubkey_db, &pk2);
		}
	}
	return true;
}

/*
 * Free the chunks cloned into chain by get_auth_chain(). It is assumed that
 * the chain array itself is local to the IKEv1 main routines.
 */
void free_auth_chain(chunk_t *chain, int chain_len)
{
	for (int i = 0; i < chain_len; i++) {
		free_chunk_content(&chain[i]);
	}
}

int get_auth_chain(chunk_t *out_chain, int chain_max,
		   const struct cert *cert, bool full_chain)
{
	if (cert == NULL)
		return 0;
	CERTCertificate *end_cert = cert->nss_cert;
	if (end_cert == NULL) {
		return 0;
	}

	/*
	 * CERT_GetDefaultCertDB() simply returns the contents of a
	 * static variable set by NSS_Initialize().  It doesn't check
	 * the value and doesn't set PR error.  Short of calling
	 * CERT_SetDefaultCertDB(NULL), the value can never be NULL.
	 */
	CERTCertDBHandle *handle = CERT_GetDefaultCertDB();
	passert(handle != NULL);

	if (!full_chain) {
		/*
		 * just the issuer unless it's a root
		 */
		CERTCertificate *is = CERT_FindCertByName(handle,
					&end_cert->derIssuer);
		if (is == NULL || is->isRoot)
			return 0;

		out_chain[0] = clone_secitem_as_chunk(is->derCert, "derCert");
		CERT_DestroyCertificate(is);
		return 1;
	}

	CERTCertificateList *chain =
		CERT_CertChainFromCert(end_cert, certUsageAnyCA, PR_FALSE);

	if (chain == NULL)
		return 0;

	if (chain->len < 1)
		return 0;

	int n = chain->len < chain_max ? chain->len : chain_max;
	int i, j;

	/* only non-root CAs in the resulting chain */
	for (i = 0, j = 0; i < n; i++) {
		if (!CERT_IsRootDERCert(&chain->certs[i]) &&
				CERT_IsCADERCert(&chain->certs[i], NULL)) {
			out_chain[j++] = clone_secitem_as_chunk(chain->certs[i], "cert");
		}
	}

	CERT_DestroyCertificateList(chain);

	return j;
}

#if defined(LIBCURL) || defined(LIBLDAP)
/*
 * Do our best to find the CA for the fetch request
 * However, this might be overkill, and only spd.this.ca should be used
 */
bool find_crl_fetch_dn(chunk_t *issuer_dn, struct connection *c)
{
	if (c->remote->config->host.ca.ptr != NULL && c->remote->config->host.ca.len > 0) {
		*issuer_dn = c->remote->config->host.ca;
		return true;
	}

	if (c->remote->config->host.cert.nss_cert != NULL) {
		*issuer_dn = same_secitem_as_chunk(c->remote->config->host.cert.nss_cert->derIssuer);
		return true;
	}

	if (c->local->config->host.ca.ptr != NULL && c->local->config->host.ca.len > 0) {
		*issuer_dn = c->local->config->host.ca;
		return true;
	}

	return false;
}
#endif

/*
 * If peer_id->kind is ID_FROMCERT, there is a guaranteed match,
 * and it will be updated to an id of kind ID_DER_ASN1_DN
 * with the name taken from the cert's derSubject.
 *
 * "certs" is a list, a certificate chain.
 * We only deal with the head and it must be an endpoint cert.
 */

diag_t match_end_cert_id(const struct certs *certs,
			 const struct id *peer_id,
			 struct id *cert_id)
{
	CERTCertificate *end_cert = certs->cert;

	if (CERT_IsCACert(end_cert, NULL)) {
		return diag("cannot use peer CA certificate");
	}

	switch (peer_id->kind) {
	case ID_IPV4_ADDR:
	case ID_IPV6_ADDR:
	case ID_FQDN:
	case ID_USER_FQDN:
	{
		/* simple match */
		/* this logs errors; no need for duplication */
		return cert_verify_subject_alt_name(end_cert, peer_id);
	}

	case ID_FROMCERT:
	{
		asn1_t end_cert_der_subject = same_secitem_as_shunk(end_cert->derSubject);
		/* adopt ID from CERT (the CERT has been verified) */
		if (DBGP(DBG_BASE)) {
			id_buf idb;
			dn_buf dnb;
			DBG_log("ID_DER_ASN1_DN '%s' does not need further ID verification; stomping on peer_id with '%s'",
				str_id(peer_id, &idb),
				str_dn(end_cert_der_subject, &dnb));
		}
		/* provide replacement */
		*cert_id = (struct id) {
			.kind = ID_DER_ASN1_DN,
			/* safe as duplicate_id() will clone this */
			.name = end_cert_der_subject,
		};
		return NULL;
	}

	case ID_DER_ASN1_DN:
	{
		asn1_t end_cert_der_subject = same_secitem_as_shunk(end_cert->derSubject);
		if (DBGP(DBG_BASE)) {
			/*
			 * Dump .derSubject as an RFC 1485 string.
			 * Include both our (str_dn()) and NSS's
			 * (.subjectName) representations; does the
			 * latter need sanitizing?
			 */
			dn_buf dnb;
			id_buf idb;
			DBG_log("comparing ID_DER_ASN1_DN '%s' to certificate derSubject='%s' (subjectName='%s')",
				str_id(peer_id, &idb),
				str_dn(end_cert_der_subject, &dnb),
				end_cert->subjectName);
		}

		int wildcards;
		bool m = match_dn_any_order_wild("",
						 end_cert_der_subject,
						 peer_id->name,
						 &wildcards);
		if (!m) {
			id_buf idb;
			return diag("peer ID_DER_ASN1_DN '%s' does not match expected '%s'",
				    end_cert->subjectName, str_id(peer_id, &idb));
		}

		if (DBGP(DBG_BASE)) {
			id_buf idb;
			DBG_log("ID_DER_ASN1_DN '%s' matched our ID '%s'",
				end_cert->subjectName,
				str_id(peer_id, &idb));
		}
		if (wildcards) {
			/* provide replacement */
			*cert_id = (struct id) {
				.kind = ID_DER_ASN1_DN,
				/* safe as duplicate_id() will clone this */
				.name = end_cert_der_subject,
			};
		}
		return NULL;
	}

	default:
	{
		esb_buf b;
		return diag("unhandled ID type %s; cannot match peer's certificate with expected peer ID",
			    enum_show(&ike_id_type_names, peer_id->kind, &b));
	}
	}
}

/*
 * Decode the CERT payload of Phase 1.
 */
/* todo:
 * https://tools.ietf.org/html/rfc4945
 *  3.3.4. PKCS #7 Wrapped X.509 Certificate
 *
 *  This type defines a particular encoding, not a particular certificate
 *  type.  Implementations SHOULD NOT generate CERTs that contain this
 *  Certificate Type.  Implementations SHOULD accept CERTs that contain
 *  this Certificate Type because several implementations are known to
 *  generate them.  Note that those implementations sometimes include
 *  entire certificate hierarchies inside a single CERT PKCS #7 payload,
 *  which violates the requirement specified in ISAKMP that this payload
 *  contain a single certificate.
 */

/*
 * Decode the certs.  If something nasty happens, such as an expired
 * cert, return false.
 *
 * Only log failures, success is left to v2_verify_certs().
 */
bool v1_decode_certs(struct msg_digest *md)
{
	struct state *st = md->v1_st;
	passert(st->st_ike_version == IKEv1);

	/*
	 * At least one set of certs have been processed; and at least
	 * once.
	 *
	 * The way this code is called is broken (see functions
	 * ikev1_decode_peer_id*() and oakley_auth()):
	 *
	 * - it is repeatedly called to decode the same cert payload
	 * (causing a cert payload the be decoded multiple times)
	 *
	 * - it is called to decode cert payloads that aren't there
	 * (for instance the first aggressive request)
	 */
	st->st_remote_certs.processed = true;

	struct payload_digest *cert_payloads = md->chain[ISAKMP_NEXT_CERT];
	if (cert_payloads == NULL) {
		return true;
	}

	if (st->st_remote_certs.verified != NULL) {
		dbg("hacking around a redundant call to v1_process_certs() - releasing verified");
		release_certs(&st->st_remote_certs.verified);
	}
	if (st->st_remote_certs.pubkey_db != NULL) {
		dbg("hacking around a redundant call to v1_process_certs() - releasing pubkey_db");
		free_public_keys(&st->st_remote_certs.pubkey_db);
	}

	if (!pexpect(st->st_remote_certs.verified == NULL)) {
		/*
		 * Since the MITM has already failed their first
		 * attempt at proving their credentials, there's no
		 * point in giving them a second chance.
		 *
		 * Happens because code rejecting the first
		 * authentication attempt leaves the state as-is
		 * instead of zombifying (where the notification is
		 * recorded and then sent, and then the state
		 * transitions to zombie where it can linger while
		 * dealing with duplicate packets) or deleting it.
		 */
		return false;
	}

	statetime_t start = statetime_start(st);
	struct connection *c = st->st_connection;

	const struct rev_opts rev_opts = {
		.ocsp = ocsp_enable,
		.ocsp_strict = ocsp_strict,
		.ocsp_post = ocsp_post,
		.crl_strict = crl_strict,
	};

	struct root_certs *root_certs = root_certs_addref(HERE); /* must-release */
	struct verified_certs certs = find_and_verify_certs(st->st_logger, st->st_ike_version,
							    cert_payloads, &rev_opts,
							    root_certs, &c->remote->host.id);
	root_certs_delref(&root_certs, HERE);

	/* either something went wrong, or there were no certs */
	if (certs.cert_chain == NULL) {
#if defined(LIBCURL) || defined(LIBLDAP)
		if (certs.crl_update_needed && deltasecs(crl_check_interval) > 0) {
			/*
			 * When a strict crl check fails, the certs
			 * are deleted and CRL_NEEDED is set.
			 *
			 * When a non-strict crl check fails, it is
			 * left to the crl fetch job to do a refresh.
			 *
			 * Trigger a refresh.
			 */
			chunk_t fdn = empty_chunk;
			if (find_crl_fetch_dn(&fdn, c)) {
				/* FDN contains issuer_dn */
				submit_crl_fetch_request(ASN1(fdn), st->st_logger);
			}
		}
#endif
		if (certs.harmless) {
			/* For instance, no CA, unknown certs, ... */
			return true;
		} else {
			log_state(RC_LOG, st,
				    "X509: certificate rejected for this connection");
			/* For instance, revoked */
			return false;
		}
	}

	pexpect(st->st_remote_certs.pubkey_db == NULL);
	st->st_remote_certs.pubkey_db = certs.pubkey_db;
	certs.pubkey_db = NULL;

	pexpect(st->st_remote_certs.verified == NULL);
	st->st_remote_certs.verified = certs.cert_chain;
	certs.cert_chain = NULL;

	statetime_stop(&start, "%s()", __func__);
	return true;
}

/*
 * Decode the CR payload of Phase 1.
 *
 *  https://tools.ietf.org/html/rfc4945
 *  3.2.4. PKCS #7 wrapped X.509 certificate
 *
 *  This ID type defines a particular encoding (not a particular
 *  certificate type); some current implementations may ignore CERTREQs
 *  they receive that contain this ID type, and the editors are unaware
 *  of any implementations that generate such CERTREQ messages.
 *  Therefore, the use of this type is deprecated.  Implementations
 *  SHOULD NOT require CERTREQs that contain this Certificate Type.
 *  Implementations that receive CERTREQs that contain this ID type MAY
 *  treat such payloads as synonymous with "X.509 Certificate -
 *  Signature".
 */

static void decode_certificate_request(struct state *st, enum ike_cert_type cert_type,
				       const struct pbs_in *pbs)
{
	switch (cert_type) {
	case CERT_X509_SIGNATURE:
	{
		asn1_t ca_name = pbs_in_left_as_shunk(pbs);

		if (DBGP(DBG_BASE)) {
			DBG_dump_hunk("CR", ca_name);
		}

		if (ca_name.len > 0) {
			err_t e = asn1_ok(ca_name);
			if (e != NULL) {
				llog(RC_LOG_SERIOUS, st->st_logger,
				     "ignoring CERTREQ payload that is not ASN1: %s", e);
				return;
			}

			generalName_t *gn = alloc_thing(generalName_t, "generalName");
			gn->name = clone_hunk(ca_name, "ca name");
			gn->kind = GN_DIRECTORY_NAME;
			gn->next = st->st_requested_ca;
			st->st_requested_ca = gn;
		}

		if (DBGP(DBG_BASE)) {
			dn_buf buf;
			DBG_log("requested CA: '%s'",
				str_dn_or_null(ca_name, "%any", &buf));
		}
		break;
	}
	default:
	{
		enum_buf b;
		llog(RC_LOG_SERIOUS, st->st_logger,
		     "ignoring CERTREQ payload of unsupported type %s",
		     str_enum(&ikev2_cert_type_names, cert_type, &b));
	}
	}
}

void decode_v1_certificate_requests(struct state *st, struct msg_digest *md)
{
	for (struct payload_digest *p = md->chain[ISAKMP_NEXT_CR]; p != NULL; p = p->next) {
		const struct isakmp_cr *const cr = &p->payload.cr;
		decode_certificate_request(st, cr->isacr_type, &p->pbs);
	}
}

void decode_v2_certificate_requests(struct state *st, struct msg_digest *md)
{
	for (struct payload_digest *p = md->chain[ISAKMP_NEXT_v2CERTREQ]; p != NULL; p = p->next) {
		const struct ikev2_certreq *const cr = &p->payload.v2certreq;
		decode_certificate_request(st, cr->isacertreq_enc, &p->pbs);
	}
}

#if 0
/*
 * returns the concatenated SHA-1 hashes of each public key in the chain
 */
static chunk_t ikev2_hash_ca_keys(x509cert_t *ca_chain)
{
	unsigned char combined_hash[SHA1_DIGEST_SIZE * 8 /*max path len*/];
	x509cert_t *ca;
	chunk_t result = EMPTY_CHUNK;
	size_t sz = 0;

	zero(&combined_hash);

	for (ca = ca_chain; ca != NULL; ca = ca->next) {
		unsigned char sighash[SHA1_DIGEST_SIZE];
		SHA1_CTX ctx_sha1;

		SHA1Init(&ctx_sha1);
		SHA1Update(&ctx_sha1, ca->signature.ptr, ca->signature.len);
		SHA1Final(sighash, &ctx_sha1);

		if (DBGP(DBG_CRYPT)) {
			DBG_dump("SHA-1 of CA signature",
				 sighash, SHA1_DIGEST_SIZE);
		}

		memcpy(combined_hash + sz, sighash, SHA1_DIGEST_SIZE);
		sz += SHA1_DIGEST_SIZE;
	}
	passert(sz <= sizeof(combined_hash));
	result = clone_bytes_as_chunk(combined_hash, sz, "combined CERTREQ hash");
	if (DBGP(DBG_CRYPT)) {
		DBG_dump_hunk("Combined CERTREQ hashes", result);
	}
	return result;
}
#endif

/*
 * Look for the existence of a non-expiring preloaded public key.
 */
bool remote_has_preloaded_pubkey(const struct state *st)
{
	const struct connection *c = st->st_connection;

	/* do not consider rw connections since
	 * the peer's identity must be known
	 */
	if (c->kind == CK_PERMANENT) {
		/* look for a matching RSA public key */
		for (const struct pubkey_list *p = pluto_pubkeys; p != NULL;
		     p = p->next) {
			const struct pubkey *key = p->key;

			if ((key->type == &pubkey_type_rsa ||
			     key->type == &pubkey_type_ecdsa) &&
			    same_id(&c->remote->host.id, &key->id) &&
			    is_realtime_epoch(key->until_time)) {
				/* found a preloaded public key */
				return true;
			}
		}
	}
	return false;
}

bool ikev1_ship_CERT(enum ike_cert_type type, shunk_t cert, pb_stream *outs)
{
	pb_stream cert_pbs;
	struct isakmp_cert cert_hd = {
		.isacert_type = type,
		.isacert_reserved = 0,
		.isacert_length = 0, /* XXX unused on sending ? */
	};

	if (!out_struct(&cert_hd, &isakmp_ipsec_certificate_desc, outs,
				&cert_pbs) ||
	    !out_hunk(cert, &cert_pbs, "CERT"))
		return false;

	close_output_pbs(&cert_pbs);
	return true;
}

bool ikev1_build_and_ship_CR(enum ike_cert_type type,
			     chunk_t ca, pb_stream *outs)
{
	pb_stream cr_pbs;
	struct isakmp_cr cr_hd = {
		.isacr_type = type,
	};

	if (!out_struct(&cr_hd, &isakmp_ipsec_cert_req_desc, outs, &cr_pbs) ||
	    (ca.ptr != NULL && !out_hunk(ca, &cr_pbs, "CA")))
		return false;

	close_output_pbs(&cr_pbs);
	return true;
}

static bool cert_has_private_key(CERTCertificate *cert, struct logger *logger)
{
	if (cert == NULL)
		return false;

	SECKEYPrivateKey *k = PK11_FindKeyByAnyCert(cert, lsw_nss_get_password_context(logger));

	if (k == NULL)
		return false;

	SECKEY_DestroyPrivateKey(k);
	return true;
}

static bool cert_time_to_str(char *buf, size_t buflen,
					CERTCertificate *cert,
					bool notbefore)
{
	PRTime notBefore_tm, notAfter_tm;

	if (CERT_GetCertTimes(cert, &notBefore_tm, &notAfter_tm) != SECSuccess)
		return false;

	PRTime ptime = notbefore ? notBefore_tm : notAfter_tm;

	PRExplodedTime printtime;

	PR_ExplodeTime(ptime, PR_GMTParameters, &printtime);

	if (!PR_FormatTime(buf, buflen, "%a %b %d %H:%M:%S %Y", &printtime))
		return false;

	return true;
}

static bool crl_time_to_str(char *buf, size_t buflen, SECItem *t)
{
	PRExplodedTime printtime;
	PRTime time;

	if (DER_DecodeTimeChoice(&time, t) != SECSuccess)
		return false;

	PR_ExplodeTime(time, PR_GMTParameters, &printtime);

	if (!PR_FormatTime(buf, buflen, "%a %b %d %H:%M:%S %Y", &printtime))
		return false;

	return true;
}

static bool cert_detail_notbefore_to_str(char *buf, size_t buflen,
					CERTCertificate *cert)
{
	return cert_time_to_str(buf, buflen, cert, true);
}

static bool cert_detail_notafter_to_str(char *buf, size_t buflen,
					CERTCertificate *cert)
{
	return cert_time_to_str(buf, buflen, cert, false);
}

static int certsntoa(CERTCertificate *cert, char *dst, size_t dstlen)
{
	return datatot(cert->serialNumber.data, cert->serialNumber.len,
			'x', dst, dstlen);
}

static void show_cert_detail(struct show *s, CERTCertificate *cert)
{
	bool is_CA = CERT_IsCACert(cert, NULL);
	bool is_root = cert->isRoot;
	SECKEYPublicKey *pub_k = SECKEY_ExtractPublicKey(&cert->subjectPublicKeyInfo);

	char sn[128];
	char *print_sn = certsntoa(cert, sn, sizeof(sn)) != 0 ? sn : "(NULL)";

	bool has_priv = cert_has_private_key(cert, show_logger(s));

	if (!pexpect(pub_k != NULL))
		return;

	KeyType pub_k_t = SECKEY_GetPublicKeyType(pub_k);

	show_comment(s, "%s%s certificate \"%s\" - SN: %s",
		     is_root ? "Root " : "",
		     is_CA ? "CA" : "End",
		     cert->nickname, print_sn);

	{
		dn_buf sbuf;
		show_comment(s, "  subject: %s",
			     dntoasi(&sbuf, cert->derSubject));
	}

	{
		dn_buf ibuf;
		show_comment(s, "  issuer: %s",
			     dntoasi(&ibuf, cert->derIssuer));
	}

	{
		char before[256];
		if (cert_detail_notbefore_to_str(before, sizeof(before), cert)) {
			show_comment(s, "  not before: %s", before);
		}
	}

	{
		char after[256];
		if (cert_detail_notafter_to_str(after, sizeof(after), cert)) {
			show_comment(s, "  not after: %s", after);
		}
	}

	show_comment(s, "  %d bit%s%s",
		     SECKEY_PublicKeyStrengthInBits(pub_k),
		     pub_k_t == rsaKey ? " RSA" : "(other)",
		     has_priv ? ": has private key" : "");
	show_blank(s);
}

typedef enum {
	CERT_TYPE_END,
	CERT_TYPE_CA,
} show_cert_t;

static bool is_cert_of_type(CERTCertificate *cert, show_cert_t type)
{
	return CERT_IsCACert(cert, NULL) == (type == CERT_TYPE_CA);
}

static void crl_detail_to_whacklog(struct show *s, CERTCrl *crl)
{
	show_blank(s);

	{
		dn_buf ibuf;

		show_comment(s, "issuer: %s",
			dntoasi(&ibuf, crl->derName));
	}

	{
		int entries = 0;

		if (crl->entries != NULL) {
			while (crl->entries[entries] != NULL)
				entries++;
		}
		show_comment(s, "revoked certs: %d", entries);
	}

	{
		char lu[256];

		if (crl_time_to_str(lu, sizeof(lu), &crl->lastUpdate))
			show_comment(s, "updates: this %s", lu);
	}

	{
		char nu[256];

		if (crl_time_to_str(nu, sizeof(nu), &crl->nextUpdate))
			show_comment(s, "         next %s", nu);
	}
}

static void crl_detail_list(struct show *s)
{
	/*
	 * CERT_GetDefaultCertDB() simply returns the contents of a
	 * static variable set by NSS_Initialize().  It doesn't check
	 * the value and doesn't set PR error.  Short of calling
	 * CERT_SetDefaultCertDB(NULL), the value can never be NULL.
	 */
	CERTCertDBHandle *handle = CERT_GetDefaultCertDB();
	passert(handle != NULL);

	show_blank(s);
	show_comment(s, "List of CRLs:");
	show_blank(s);

	CERTCrlHeadNode *crl_list = NULL;

	if (SEC_LookupCrls(handle, &crl_list, SEC_CRL_TYPE) != SECSuccess)
		return;

	for (CERTCrlNode *crl_node = crl_list->first; crl_node != NULL;
	     crl_node = crl_node->next) {
		if (crl_node->crl != NULL) {
			crl_detail_to_whacklog(s, &crl_node->crl->crl);
		}
	}
	dbg("releasing crl list in %s", __func__);
	PORT_FreeArena(crl_list->arena, PR_FALSE);
}

CERTCertList *get_all_certificates(struct logger *logger)
{
	PK11SlotInfo *slot = lsw_nss_get_authenticated_slot(logger);
	if (slot == NULL) {
		/* already logged */
		return NULL;
	}
	return PK11_ListCertsInSlot(slot);
}

static void cert_detail_list(struct show *s, show_cert_t type)
{
	char *tstr;

	switch (type) {
	case CERT_TYPE_END:
		tstr = "End";
		break;
	case CERT_TYPE_CA:
		tstr = "CA";
		break;
	default:
		bad_case(type);
	}

	show_blank(s);
	show_comment(s, "List of X.509 %s Certificates:", tstr);
	show_blank(s);

	CERTCertList *certs = get_all_certificates(show_logger(s));

	if (certs == NULL)
		return;

	for (CERTCertListNode *node = CERT_LIST_HEAD(certs);
	     !CERT_LIST_END(node, certs); node = CERT_LIST_NEXT(node)) {
		if (is_cert_of_type(node->cert, type)) {
			show_cert_detail(s, node->cert);
		}
	}

	CERT_DestroyCertList(certs);
}

void list_crls(struct show *s)
{
	crl_detail_list(s);
}

void list_certs(struct show *s)
{
	cert_detail_list(s, CERT_TYPE_END);
}

/*
 * Either the underlying cert's nickname, or NULL.
 */
const char *cert_nickname(const cert_t *cert)
{
	return cert != NULL && cert->nss_cert != NULL ? cert->nss_cert->nickname : NULL;
}

void list_authcerts(struct show *s)
{
	cert_detail_list(s, CERT_TYPE_CA);
}

void clear_ocsp_cache(void)
{
	dbg("calling NSS to clear OCSP cache");
	(void)CERT_ClearOCSPCache();
}
