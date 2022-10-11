/* iface, for libreswan
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2002, 2013,2016 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2003-2008 Michael C Richardson <mcr@xelerance.com>
 * Copyright (C) 2003-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2008-2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2009 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2010 Tuomo Soini <tis@foobar.fi>
 * Copyright (C) 2012-2017 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2013 Wolfgang Nothdurft <wolfgang@linogate.de>
 * Copyright (C) 2016-2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2017 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2017 Mayank Totale <mtotale@gmail.com>
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

#include <sys/ioctl.h>
#include <net/if.h>

#include "socketwrapper.h"		/* for safe_sock() */

#include "ip_info.h"

#include "defs.h"
#include "log.h"
#include "kernel_iface.h"

/*
 * Process the updated list of interfaces.
 *
 * On linux, see netdevice(7) (and note that it clearly documents that
 * the below code only works with IPv4).
 *
 * On BSD, see netintro(4).  On BSD, <<struct ifreq>> includes
 * sockaddr_storage in its union of addresses and that is big enough
 * for any address so this should also work for IPv6.
 */

struct raw_iface *find_raw_ifaces(const struct ip_info *afi, struct logger *logger)
{
	/* Get a UDP socket */
	dbg("finding raw interfaces of type %s", afi->ip_name);


	int udp_sock = safe_socket(afi->pf, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_sock == -1) {
		fatal_errno(PLUTO_EXIT_FAIL, logger, errno,
			    "find %s interfaces failed calling socket(%s, SOCK_DGRAM, IPPROTO_UDP)",
			    afi->ip_name, afi->pf_name);
	}

	/*
	 * Without SO_REUSEADDR, bind() of udp_sock will cause
	 * 'address already in use?
	 */
	static const int on = true;     /* by-reference parameter; constant, we hope */
	if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR,
		       (const void *)&on, sizeof(on)) < 0) {
		fatal_errno(PLUTO_EXIT_FAIL, logger, errno,
			    "find %s interfaces failed calling setsockopt(SOL_SOCKET, SO_REUSEADDR)",
			    afi->ip_name);
	}

	/*
	 * Build an "any" endpoint and then bind the socket.
	 */
	ip_endpoint any_ep = endpoint_from_address_protocol_port(afi->address.unspec,
								 &ip_protocol_udp,
								 ip_hport(IKE_UDP_PORT));
	ip_sockaddr any_sa = sockaddr_from_endpoint(any_ep);
	if (bind(udp_sock, &any_sa.sa.sa, any_sa.len) < 0) {
		endpoint_buf eb;
		fatal_errno(PLUTO_EXIT_FAIL, logger, errno,
			    "find %s interfaces failed calling bind(%s)",
			    afi->ip_name, str_endpoint(&any_ep, &eb));
	}

	/*
	 * Load buf with array of raw interfaces from kernel.
	 *
	 * We have to guess at the upper bound (num).  If we guess
	 * low, double num and repeat.  But don't go crazy: stop
	 * before 1024**2.
	 *
	 * Tricky: num is a static so that we won't have to start from
	 * 64 in subsequent calls to find_raw_ifaces4.
	 */
	static volatile int num = 64; /* not very thread safe */
	struct ifconf ifconf = { .ifc_len = 0, };
	void *buf = NULL;	/* for list of interfaces -- arbitrary limit */
	for (; num < (1024 * 1024); num *= 2) {
		/* Get num local interfaces.  See netdevice(7). */
		int len = num * sizeof(struct ifreq);
		realloc_bytes(&buf, ifconf.ifc_len, len, "ifreq");

		ifconf = (struct ifconf) {
			.ifc_len = len,
			.ifc_buf = (void*)buf,
		};

		if (ioctl(udp_sock, SIOCGIFCONF, &ifconf) == -1) {
			fatal_errno(PLUTO_EXIT_FAIL, logger, errno,
				    "find %s interfaces failed calling ioctl(SIOCGIFCONF)",
				    afi->ip_name);
		}

		/* if we got back less than we asked for, we have them all */
		if (ifconf.ifc_len < len) {
			break;
		}
	}

	unsigned nr_req = (ifconf.ifc_len / sizeof(struct ifreq));
	dbg("ioctl(IOCGIFCONF) returned %u %s interfaces", nr_req, afi->ip_name);

	/*
	 * Add an entry to rifaces for each interesting interface.
	 */
	struct raw_iface *rifaces = NULL;
	for (const struct ifreq *ifr = ifconf.ifc_req; ifr < ifconf.ifc_req + nr_req; ifr++) {

		/* build a NUL-terminated copy of the rname field */
		char ifname[IFNAMSIZ + 1];
		memcpy(ifname, ifr->ifr_name, IFNAMSIZ);
		ifname[IFNAMSIZ] = '\0';

		/* ignore all but AF_INET interfaces */
		if (ifr->ifr_addr.sa_family != afi->af) {
			dbg("  ignoring non-%s interface %s with family %d",
			    afi->ip_name, ifname, ifr->ifr_addr.sa_family);
			continue; /* not interesting */
		}

		ip_address addr;
		err_t e = sockaddr_to_address_port(&ifr->ifr_addr, sizeof(ifr->ifr_ifru),
						   &addr, NULL/*port*/);
		if (e != NULL) {
			dbg("  ignoring %s interface %s: %s",
			    afi->ip_name, ifname, e);
			continue; /* ignore slave interfaces; they share IPs with their master */
		}

		/* ignore all but AF_INET interfaces (redundant) */
		if (!pexpect(address_type(&addr) == afi)) {
			dbg("  ignoring non-%s interface %s",
			    afi->ip_name, ifname);
			continue; /* not interesting */
		}

		/* ignore unconfigured interfaces */
		if (!address_is_specified(addr)) {
			address_buf ab;
			dbg("  ignoring %s interface %s with unspecified address %s",
			    afi->ip_name, ifname, str_address(&addr, &ab));
			continue;
		}

#if 0
		if (address_is_loopback(addr)) {
			address_buf ab;
			dbg("  ignoring %s interface %s with loopback address %s",
			    afi->ip_name, ifname, str_address(&addr, &ab));
			continue;
		}
#endif

		/*
		 * Find out stuff about this interface.  See
		 * netdevice(7) or netintro(4).
		 */
		struct ifreq auxinfo = {0};
		passert(sizeof(auxinfo.ifr_name) == sizeof(ifr->ifr_name)); /* duh! */
		memcpy(auxinfo.ifr_name, ifr->ifr_name, IFNAMSIZ);
		if (ioctl(udp_sock, SIOCGIFFLAGS, &auxinfo) == -1) {
			log_errno(logger, errno,
				  "ignored %s interface %s - ioctl(SIOCGIFFLAGS) failed",
				  afi->ip_name, ifname);
			continue; /* happens when using device with label? */
		}

		if (!(auxinfo.ifr_flags & IFF_UP)) {
			dbg("  ignoring non-up %s interface %s",
			    afi->ip_name, ifname);
			continue; /* ignore an interface that isn't UP */
		}
#ifdef IFF_SLAVE
		/* only linux ... */
		if (auxinfo.ifr_flags & IFF_SLAVE) {
			dbg("  ignoring slave %s interface %s",
			    afi->ip_name, ifname);
			continue; /* ignore slave interfaces; they share IPs with their master */
		}
#endif

		struct raw_iface *ri = overalloc_thing(struct raw_iface,
						       strlen(ifname) + 1,
						       "iface");
		ri->addr = addr;
		strcpy(ri->name, ifname);
		ri->next = rifaces;
		rifaces = ri;
		ipstr_buf b;
		dbg("  found %s interface %s with address %s",
		    afi->ip_name, ri->name, ipstr(&ri->addr, &b));
	}

	pfree(buf);
	close(udp_sock);
	return rifaces;
}
