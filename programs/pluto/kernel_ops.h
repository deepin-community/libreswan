/* kernel operation wrappers, for libreswan
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

#ifndef KERNEL_OPS_H
#define KERNEL_OPS_H

#include "kernel.h"

/*kernel_ops_policy() kernel_ops_spd()? */
extern bool raw_policy(enum kernel_policy_op op,
		       enum expect_kernel_policy expect_kernel_policy,
		       const ip_selector *this_client,
		       const ip_selector *that_client,
		       enum shunt_policy shunt_policy,
		       const struct kernel_policy *policy,
		       deltatime_t use_lifetime,
		       uint32_t sa_priority,
		       const struct sa_marks *sa_marks,
		       const struct pluto_xfrmi *xfrmi,
		       const shunk_t sec_label,
		       struct logger *logger,
		       const char *fmt, ...) PRINTF_LIKE(13);

/*kernel_ops_state()? kernel_ops_sad()?*/
bool kernel_ops_add_sa(const struct kernel_sa *sa,
		       bool replace,
		       struct logger *logger);

ipsec_spi_t kernel_ops_get_ipsec_spi(ipsec_spi_t avoid,
				     const ip_address *src,
				     const ip_address *dst,
				     const struct ip_protocol *proto,
				     bool tunnel_mode,
				     reqid_t reqid,
				     uintmax_t min, uintmax_t max,
				     const char *story,	/* often SAID string */
				     struct logger *logger);

bool kernel_ops_del_ipsec_spi(ipsec_spi_t spi, const struct ip_protocol *proto,
			      const ip_address *src, const ip_address *dst,
			      struct logger *logger);

#endif
