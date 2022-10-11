/* kernel op wrappers, for libreswan
 *
 * Copyright (C) 2021 Andrew Cagney <cagney@gnu.org>
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

#include "ip_encap.h"

#include "kernel.h"
#include "kernel_ops.h"
#include "log.h"
#include "kernel_xfrm_interface.h"

/*
 * Setup an IPsec route entry.
 *
 * There's lots of redundancy here, see debug log lines below.
 */

bool raw_policy(enum kernel_policy_op op,
		enum expect_kernel_policy expect_kernel_policy,
		const ip_selector *src_client,
		const ip_selector *dst_client,
		enum shunt_policy shunt_policy,
		const struct kernel_policy *kernel_policy,
		deltatime_t use_lifetime,
		uint32_t sa_priority,
		const struct sa_marks *sa_marks,
		const struct pluto_xfrmi *xfrmi,
		const shunk_t sec_label,
		struct logger *logger,
		const char *fmt, ...)
{
	const ip_protocol *src_client_proto = selector_protocol(*src_client);
	const ip_protocol *dst_client_proto = selector_protocol(*dst_client);

	LSWDBGP(DBG_BASE, buf) {

		jam(buf, "kernel: %s() ", __func__);
		jam_enum_short(buf, &kernel_policy_op_names, op);
		if (kernel_policy != NULL &&
		    kernel_policy->nr_rules > 0 &&
		    (op == KP_DELETE_OUTBOUND || op == KP_DELETE_INBOUND)) {
			jam(buf, ",ENCAP!=NULL");
		}

		jam_string(buf, " ");
		jam_string(buf, expect_kernel_policy_name(expect_kernel_policy));

		jam(buf, " ");
		va_list ap;
		va_start(ap, fmt);
		jam_va_list(buf, fmt, ap);
		va_end(ap);

		jam(buf, " client=");
		jam_selectors(buf, src_client, dst_client);

		jam_string(buf, " shunt_policy=");
		jam_enum_short(buf, &shunt_policy_names, shunt_policy);

		jam(buf, " kernel_policy=");
		if (kernel_policy == NULL) {
			jam(buf, "<null>");
		} else {
			jam_address(buf, &kernel_policy->src.host);
			jam(buf, "==>");
			jam_address(buf, &kernel_policy->dst.host);
			jam(buf, ",mode=%s", encap_mode_name(kernel_policy->mode));

			jam_string(buf, "rule=[(inner)");
			const char *sep = "";
			for (unsigned i = 1; i <= kernel_policy->nr_rules; i++) {
				const struct kernel_policy_rule *rule = &kernel_policy->rule[i];
				const ip_protocol *rule_proto = protocol_by_ipproto(rule->proto);
				jam(buf, "%s%s(%d)", sep, rule_proto->name, rule->reqid);
				sep = ",";
			}
			jam(buf, "(outer)]");
		}

		jam(buf, " lifetime=");
		jam_deltatime(buf, use_lifetime);
		jam(buf, "s");

		jam(buf, " priority=%d", sa_priority);

		if (sa_marks != NULL) {
			jam(buf, " sa_marks=");
			const char *dir = "out:";
			FOR_EACH_THING(mark, &sa_marks->out, &sa_marks->in) {
				jam(buf, "%s%x/%x%s",
				    dir, mark->val, mark->mask,
				    mark->unique ? "/unique" : "");
				dir = ",in:";
			}
		}

		jam(buf, " xfrm_if_id=%d",
		    xfrmi != NULL ? (int)xfrmi->if_id : -1);

		jam(buf, " sec_label=");
		jam_sanitized_hunk(buf, sec_label);

	}

	pexpect(src_client_proto == dst_client_proto);

	switch(shunt_policy) {
	case SHUNT_HOLD:
		dbg("kernel: %s() SPI_HOLD implemented as no-op", __func__);
		return true;
	case SHUNT_TRAP:
		if (op == KP_ADD_INBOUND ||
		    op == KP_DELETE_INBOUND) {
			dbg("kernel: %s() SPI_TRAP inbound implemented as no-op", __func__);
			return true;
		}
		break;
	default:
		break;
	}

	bool result = kernel_ops->raw_policy(op, expect_kernel_policy,
					     src_client, dst_client,
					     shunt_policy,
					     kernel_policy,
					     use_lifetime, sa_priority,
					     sa_marks, xfrmi,
					     sec_label,
					     logger);
	dbg("kernel: policy: result=%s", result ? "success" : "failed");

	return result;
}

bool kernel_ops_add_sa(const struct kernel_sa *sa, bool replace, struct logger *logger)
{
	LSWDBGP(DBG_BASE, buf) {

		const ip_protocol *src_proto = selector_protocol(*sa->src.client);
		const ip_protocol *dst_proto = selector_protocol(*sa->dst.client);
		const ip_protocol *esa_proto = protocol_by_ipproto(sa->esatype);
		const ip_protocol *transport_proto = protocol_by_ipproto(sa->transport_proto);

		jam(buf, "kernel: add_sa()");

		jam(buf, " level=%d", sa->level);
		jam(buf, " %s", sa->inbound ? "inbound" : "outbound");
		jam(buf, " %s", sa->tunnel ? "tunnel" : "transport");

		jam(buf, " ");
		jam_selector_subnet_port(buf, sa->src.client);
		jam(buf, "-%s->", src_proto->name);
		jam_address(buf, sa->src.address);
		jam(buf, "==%s", esa_proto->name);
		jam(buf, "["PRI_IPSEC_SPI"]", pri_ipsec_spi(sa->spi));
		if (sa->encap_type != NULL) {
			jam(buf, "=%s", sa->encap_type->name);
		}
		jam(buf, "==>");
		jam_address(buf, sa->dst.address);
		jam(buf, "-%s->", dst_proto->name);
		jam_selector_subnet_port(buf, sa->dst.client);

		if (sa->esn) jam(buf, " +esn");
		if (sa->decap_dscp) jam(buf, " +decap_dscp");
		if (sa->nopmtudisc) jam(buf, " +nopmtudisc");

		jam(buf, " replay_window=%d", sa->replay_window);
		jam(buf, " transport_proto=%s", transport_proto->name);

		if (sa->ipcomp != NULL) {
			jam(buf, " %s", sa->ipcomp->common.fqn);
		}
		if (sa->integ != NULL) {
			jam(buf, " %s:%d", sa->integ->common.fqn, sa->authkeylen);
		}
		if (sa->encrypt != NULL) {
			jam(buf, " %s:%d", sa->encrypt->common.fqn, sa->enckeylen);
		}
	}
	return kernel_ops->add_sa(sa, replace, logger);
}

bool migrate_ipsec_sa(struct child_sa *child)
{
	if (kernel_ops->migrate_ipsec_sa != NULL) {
		return kernel_ops->migrate_ipsec_sa(child);
	} else {
		dbg("kernel: Unsupported kernel stack in migrate_ipsec_sa");
		return false;
	}
}

ipsec_spi_t kernel_ops_get_ipsec_spi(ipsec_spi_t avoid,
				     const ip_address *src,
				     const ip_address *dst,
				     const struct ip_protocol *proto,
				     bool tunnel_mode,
				     reqid_t reqid,
				     uintmax_t min, uintmax_t max,
				     const char *story,	/* often SAID string */
				     struct logger *logger)
{
	LDBG(logger, buf) {
		jam_string(buf, "kernel: get_ipsec_spi() ");
		jam_address(buf, src);
		jam_string(buf, "-");
		jam(buf, "%s", proto->name);
		jam_string(buf, "->");
		jam_address(buf, dst);
		jam_string(buf, (tunnel_mode ? " [tunnel]" : " [transport]"));
		jam(buf, " reqid=%x", reqid);
		jam(buf, " [%jx,%jx]", min, max);
		jam(buf, " for %s ...", story);
	}

	passert(kernel_ops->get_ipsec_spi != NULL);
	ipsec_spi_t spi = kernel_ops->get_ipsec_spi(avoid, src, dst, proto, tunnel_mode,
						    reqid, min, max, story, logger);
	ldbg(logger, "kernel: get_ipsec_spi() ... allocated "PRI_IPSEC_SPI" for %s",
	     pri_ipsec_spi(spi), story);

	return spi;
}

bool kernel_ops_del_ipsec_spi(ipsec_spi_t spi, const struct ip_protocol *proto,
			      const ip_address *src, const ip_address *dst,
			      struct logger *logger)
{
	ip_said said = said_from_address_protocol_spi(*dst, proto, spi);
	said_buf sbuf;
	const char *said_story = str_said(&said, &sbuf);

	address_buf sb, db;
	dbg("kernel: del_ipsec_spi() deleting sa %s-%s["PRI_IPSEC_SPI"]->%s for %s ...",
	    str_address(src, &sb),
	    proto == NULL ? "<NULL>" : proto->name,
	    pri_ipsec_spi(spi),
	    str_address(dst, &db),
	    said_story);

	passert(kernel_ops->del_ipsec_spi != NULL);
	bool ok =kernel_ops->del_ipsec_spi(spi, proto, src, dst, said_story, logger);
	ldbg(logger, "kernel: get_ipsec_spi() ... %s", ok ? "succeeded" : "failed");

	return ok;
}

bool kernel_ops_detect_offload(const struct raw_iface *ifp, struct logger *logger)
{
	static bool no_offload;
	if (no_offload) {
		ldbg(logger, "no offload already detected");
		return false;
	}

	if (kernel_ops->detect_offload == NULL) {
		ldbg(logger, "%s kernel interface does not support offload",
		     kernel_ops->interface_name);
		no_offload = true;
		return false;
	}

	return kernel_ops->detect_offload(ifp, logger);
}
