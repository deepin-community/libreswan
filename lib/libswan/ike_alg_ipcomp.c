/* IKE modular algorithm handling interface, for libreswan
 *
 * Copyright (C) 2022 Andrew Cagney
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
#include <string.h>
#include <stddef.h>
#include <sys/types.h>

#include "constants.h"
#include "lswlog.h"
#include "ike_alg.h"

#include "ike_alg_ipcomp.h"
#include "ike_alg_ipcomp_ops.h"

#include "lsw-pfkeyv2.h"

const struct ipcomp_desc ike_alg_ipcomp_deflate = {
	.common = {
		.algo_type = IKE_ALG_IPCOMP,
		.fqn = "DEFLATE",
		.names = "deflate",
		.id = {
			[IKEv1_OAKLEY_ID] = -1,
			[IKEv1_ESP_ID] = IPCOMP_DEFLATE,
			[IKEv2_ALG_ID] = IPCOMP_DEFLATE,
		},
		.fips = true, /* it's meaningless */
	},
	.kernel = {
		.xfrm_name = "deflate",
#ifdef SADB_X_CALG_DEFLATE
		.sadb_calg_id = SADB_X_CALG_DEFLATE,
#endif
	},
};

const struct ipcomp_desc ike_alg_ipcomp_lzs = {
	.common = {
		.algo_type = IKE_ALG_IPCOMP,
		.fqn = "LZS",
		.names = "lzs",
		.id = {
			[IKEv1_OAKLEY_ID] = -1,
			[IKEv1_ESP_ID] = -1, /*IPCOMP_LZS*/
			[IKEv2_ALG_ID] = IPCOMP_LZS,
		},
		.fips = true, /* it's meaningless */
	},
	.kernel = {
		.xfrm_name = "lzs",
#ifdef SADB_X_CALG_LZS
		.sadb_calg_id = SADB_X_CALG_LZS,
#endif
	},
};


const struct ipcomp_desc ike_alg_ipcomp_lzjh = {
	.common = {
		.algo_type = IKE_ALG_IPCOMP,
		.fqn = "LZJH",
		.names = "lzjh",
		.id = {
			[IKEv1_OAKLEY_ID] = -1,
			[IKEv1_ESP_ID] = -1, /*IPCOMP_LZJH*/
			[IKEv2_ALG_ID] = IPCOMP_LZJH,
		},
		.fips = true, /* it's meaningless */
	},
	.kernel = {
		.xfrm_name = "lzjh",
#ifdef SADB_X_CALG_LZJH
		.sadb_calg_id = SADB_X_CALG_LZJH,
#endif
	},
};
