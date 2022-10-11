/* Interface to the PF_KEY v2 IPsec mechanism, for Libreswan
 *
 * Copyright (C)  2022  Andrew Cagney
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

#ifndef KERNEL_SADB_H
#define KERNEL_SADB_H

#include <stdbool.h>
#include <stddef.h>		/* for size_t */
#include <netinet/in.h>		/* for IPPROTO_* */
#include <stdint.h>		/* because pfkeyv2.h doesn't */

#include "lsw-pfkeyv2.h"	/* also pulls in missing types dependencies */
#ifdef linux
# include <linux/ipsec.h>
#else
# include <netipsec/ipsec.h>
#endif

#include "lswcdefs.h"
#include "shunk.h"
#include "sparse_names.h"
#include "ip_address.h"
#include "ip_port.h"

struct logger;
struct ike_alg;
struct jambuf;

enum sadb_type {
	sadb_acquire = SADB_ACQUIRE,
	sadb_add = SADB_ADD,
	sadb_delete = SADB_DELETE,
	sadb_get = SADB_GET,
	sadb_getspi = SADB_GETSPI,
	sadb_register = SADB_REGISTER,
	sadb_update = SADB_UPDATE,
	sadb_x_spdadd = SADB_X_SPDADD,
	sadb_x_spddelete = SADB_X_SPDDELETE,
	sadb_x_spdupdate = SADB_X_SPDUPDATE,
};

enum sadb_satype {
	sadb_satype_ah = SADB_SATYPE_AH,
	sadb_satype_esp = SADB_SATYPE_ESP,
	sadb_satype_unspec = SADB_SATYPE_UNSPEC,
	sadb_x_satype_ipcomp = SADB_X_SATYPE_IPCOMP,
};

enum sadb_exttype {
	sadb_ext_address_dst = SADB_EXT_ADDRESS_DST,
	sadb_ext_address_src = SADB_EXT_ADDRESS_SRC,
	sadb_ext_key_auth = SADB_EXT_KEY_AUTH,
	sadb_ext_key_encrypt = SADB_EXT_KEY_ENCRYPT,
	sadb_ext_lifetime_current = SADB_EXT_LIFETIME_CURRENT,
	sadb_ext_lifetime_hard = SADB_EXT_LIFETIME_HARD,
	sadb_ext_lifetime_soft = SADB_EXT_LIFETIME_SOFT,
	sadb_ext_proposal = SADB_EXT_PROPOSAL,
	sadb_ext_sa = SADB_EXT_SA,
	sadb_ext_spirange = SADB_EXT_SPIRANGE,
	sadb_ext_supported_auth = SADB_EXT_SUPPORTED_AUTH,
	sadb_ext_supported_encrypt = SADB_EXT_SUPPORTED_ENCRYPT,
	sadb_x_ext_nat_t_type = SADB_X_EXT_NAT_T_TYPE,
	sadb_x_ext_policy = SADB_X_EXT_POLICY,
	sadb_x_ext_sa2 = SADB_X_EXT_SA2,
};

enum sadb_sastate {
	sadb_sastate_dead = SADB_SASTATE_DEAD,
	sadb_sastate_dying = SADB_SASTATE_DYING,
	sadb_sastate_larval = SADB_SASTATE_LARVAL,
	sadb_sastate_mature = SADB_SASTATE_MATURE,
};

enum ipsec_policy {
	ipsec_policy_discard = IPSEC_POLICY_DISCARD,
	ipsec_policy_ipsec = IPSEC_POLICY_IPSEC,
	ipsec_policy_none = IPSEC_POLICY_NONE,
};

enum ipsec_dir {
	ipsec_dir_inbound = IPSEC_DIR_INBOUND,
	ipsec_dir_outbound = IPSEC_DIR_OUTBOUND,
};

enum ipsec_mode {
	ipsec_mode_any = 0,
	ipsec_mode_transport = IPSEC_MODE_TRANSPORT,
	ipsec_mode_tunnel = IPSEC_MODE_TUNNEL,
};

enum ipsec_proto {
	ipsec_proto_ah = IPPROTO_AH,
	ipsec_proto_esp = IPPROTO_ESP,
	ipsec_proto_ipip = IPPROTO_IPIP,
	ipsec_proto_any = IPSEC_PROTO_ANY, /* 255, akaIPSEC_ULPROTO_ANY */
#ifdef IPPROTO_IPCOMP
	ipsec_proto_ipcomp = IPPROTO_IPCOMP,
#endif
#ifdef IPPROTO_COMP
	ipsec_proto_ipcomp = IPPROTO_COMP,
#endif
};

enum ipsec_level {
	ipsec_level_require = IPSEC_LEVEL_REQUIRE,
};


extern sparse_names sadb_aalg_names;
extern sparse_names sadb_calg_names;
extern sparse_names sadb_ealg_names;
extern sparse_names sadb_exttype_names;
extern sparse_names sadb_flow_type_names;
extern sparse_names sadb_identtype_names;
extern sparse_names sadb_lifetime_names;
extern sparse_names sadb_policyflag_names;
extern sparse_names sadb_saflags_names;
extern sparse_names sadb_sastate_names;
extern sparse_names sadb_satype_names;
extern sparse_names sadb_type_names;

extern sparse_sparse_names sadb_alg_names;
extern sparse_sparse_names sadb_satype_ealg_names;
extern sparse_sparse_names sadb_satype_aalg_names;

extern sparse_names ipsec_policy_names;
extern sparse_names ipsec_dir_names;
extern sparse_names ipsec_mode_names;

#define ldbg_msg(LOGGER, PTR, LEN, FMT, ...)				\
	{								\
		if (DBGP(DBG_BASE)) {					\
			DBG_msg(LOGGER, PTR, LEN, FMT, ##__VA_ARGS__);	\
		}							\
	}

void DBG_msg(struct logger *logger, const void *ptr, size_t len, const char *fmt, ...) PRINTF_LIKE(4);

#define ldbg_sadb_alg(LOGGER, EXTTYPE, M, WHAT)			\
	{							\
		if (DBGP(DBG_BASE)) {				\
			DBG_sadb_alg(LOGGER, EXTTYPE, M, WHAT);	\
		}						\
	}

#define ldbg_sadb_sa(LOGGER, EXTTYPE, M, WHAT)			\
	{							\
		if (DBGP(DBG_BASE)) {				\
			DBG_sadb_sa(LOGGER, EXTTYPE, M, WHAT);	\
		}						\
	}

void DBG_sadb_alg(struct logger *logger, enum sadb_exttype, const struct sadb_alg *m, const char *what);
void DBG_sadb_sa(struct logger *logger, enum sadb_satype, const struct sadb_sa *m, const char *what);
void jam_sadb_alg(struct jambuf *buf, enum sadb_exttype, const struct sadb_alg *m);
void jam_sadb_sa(struct jambuf *buf, enum sadb_satype, const struct sadb_sa *m);

#define DD(TYPE, ...)							\
	struct TYPE;							\
	void ldbg_##TYPE(struct logger *logger,				\
			 ##__VA_ARGS__, const struct TYPE *m,		\
			 const char *what);				\
	void DBG_##TYPE(struct logger *logger,				\
			##__VA_ARGS__, const struct TYPE *m,		\
			const char *what);				\
	void jam_##TYPE(struct jambuf *buf,				\
			##__VA_ARGS__, const struct TYPE *m)

DD(sadb_address);
DD(sadb_comb);
DD(sadb_ext);
DD(sadb_ident);
DD(sadb_key);
DD(sadb_lifetime);
DD(sadb_msg);
DD(sadb_prop);
DD(sadb_proposal);
DD(sadb_sens);
DD(sadb_spirange);
DD(sadb_supported);
DD(sadb_x_ipsecrequest);
DD(sadb_x_nat_t_frag);
DD(sadb_x_nat_t_port);
DD(sadb_x_nat_t_type);
DD(sadb_x_policy);
DD(sadb_x_sa2);

#undef DD

bool get_sadb_sockaddr_address_port(shunk_t *cursor,
				    ip_address *address,
				    ip_port *port,
				    struct logger *logger);
#define GET_SADB(TYPE)							\
	const struct TYPE *get_##TYPE(shunk_t *cursor, shunk_t *type_cursor, struct logger *logger);

GET_SADB(sadb_address);
GET_SADB(sadb_comb);
GET_SADB(sadb_ext);
GET_SADB(sadb_ident);
GET_SADB(sadb_key);
GET_SADB(sadb_lifetime);
GET_SADB(sadb_msg);
GET_SADB(sadb_prop);
GET_SADB(sadb_proposal);
GET_SADB(sadb_sa);
GET_SADB(sadb_sens);
GET_SADB(sadb_spirange);
GET_SADB(sadb_supported);
GET_SADB(sadb_x_ipsecrequest);
GET_SADB(sadb_x_policy);
GET_SADB(sadb_x_nat_t_type);
GET_SADB(sadb_x_sa2);

#undef GET_SADB

#endif
