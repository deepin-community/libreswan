/* up selector, for libreswan
 *
 * Copyright (C) 2020  Andrew Cagney
 * Copyright (C) 2000  Henry Spencer.
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

#include "lswlog.h"

#include "ip_packet.h"
#include "ip_info.h"
#include "ip_protocol.h"

const ip_packet unset_packet;

ip_packet packet_from_raw(where_t where,
			  /* INFO determines meaning of BYTES */
			  const struct ip_info *info,
			  const struct ip_bytes *src_bytes,
			  const struct ip_bytes *dst_bytes,
			  /* PROTOCOL determines meaning of PORTs */
			  const struct ip_protocol *protocol,
			  ip_port src_port, ip_port dst_port)
{
	ip_packet packet = {
		.is_set = true,
		.info = info,
		.protocol = protocol,
		.src = {
			.bytes = *src_bytes,
			.hport = src_port.hport, /* can be zero */
		},
		.dst = {
			.bytes = *dst_bytes,
			.hport = dst_port.hport,
		},
	};
	pexpect_packet(&packet, where);
	return packet;
}

static const char *unset(const ip_packet *packet)
{
	return IP_INFO_PROTOCOL_UNSET(packet);
}

ip_address packet_src_address(const ip_packet packet)
{
	if (packet.is_set) {
		return address_from_raw(HERE,
					 packet.info->ip_version,
					 packet.src.bytes);
	} else {
		return unset_address;
	}
}

ip_address packet_dst_address(const ip_packet packet)
{
	if (packet.is_set) {
		return address_from_raw(HERE,
					 packet.info->ip_version,
					 packet.dst.bytes);
	} else {
		return unset_address;
	}
}

ip_endpoint packet_dst_endpoint(const ip_packet packet)
{
	if (packet.is_set) {
		return endpoint_from_raw(HERE,
					 packet.info->ip_version,
					 packet.dst.bytes,
					 packet.protocol,
					 ip_hport(packet.dst.hport));
	} else {
		return unset_endpoint;
	}
}

ip_selector packet_src_selector(const ip_packet packet)
{
	if (packet.is_set) {
		return selector_from_raw(HERE,
					 packet.info->ip_version,
					 packet.src.bytes,
					 packet.info->mask_cnt,
					 packet.protocol,
					 ip_hport(packet.src.hport));
	} else {
		return unset_selector;
	}
}

ip_selector packet_dst_selector(const ip_packet packet)
{
	if (packet.is_set) {
		return selector_from_raw(HERE,
					 packet.info->ip_version,
					 packet.dst.bytes,
					 packet.info->mask_cnt,
					 packet.protocol,
					 ip_hport(packet.dst.hport));
	} else {
		return unset_selector;
	}
}

size_t jam_packet(struct jambuf *buf, const ip_packet *packet)
{
	const char *u = unset(packet);
	if (u != NULL) {
		return jam(buf, "<packet-%s>", u);
	}

	const struct ip_info *afi = packet->info;

	size_t s = 0;
	/* src port can be zero aka wildcard */
	if (packet->src.hport == 0 && packet->protocol->endpoint_requires_non_zero_port) {
		/* For IPv6 includes [] */
		s += afi->address.jam_wrapped(buf, afi, &packet->src.bytes);
	} else {
		s += afi->endpoint.jam(buf, afi, &packet->src.bytes, packet->src.hport);
	}
	/* dst port is always valid */
	s += jam(buf, "-%s->", packet->protocol->name);
	s += afi->endpoint.jam(buf, afi, &packet->dst.bytes, packet->dst.hport);
	return s;
}

const char *str_packet(const ip_packet *packet, packet_buf *dst)
{
	struct jambuf buf = ARRAY_AS_JAMBUF(dst->buf);
	jam_packet(&buf, packet);
	return dst->buf;
}

void pexpect_packet(const ip_packet *packet, where_t where)
{
	if (packet == NULL) {
		llog_pexpect(&global_logger, where, "null packet");
		return;
	}

	if (packet->info == NULL ||
	    packet->protocol == NULL ||
	    ip_bytes_is_zero(&packet->src.bytes) ||
	    ip_bytes_is_zero(&packet->dst.bytes) ||
	    /*
	     * An acquire triggered by a packet with no specified
	     * source port will have a zero source port.
	     */
	    (packet->protocol->endpoint_requires_non_zero_port && packet->dst.hport == 0)) {
		if (packet->is_set) {
			llog_pexpect(&global_logger, where,
				     "invalid packet: "PRI_PACKET, pri_packet(packet));

		}
	} else if (!packet->is_set) {
		llog_pexpect(&global_logger, where,
			     "invalid packet: "PRI_PACKET, pri_packet(packet));
	}
}
