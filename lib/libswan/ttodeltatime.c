/* time conversion, for libreswan
 *
 * Copyright (C) 2022 Andrew Cagney <cagney@gnu.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/lgpl-2.1.txt>.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 * License for more details.
 *
 */

#include "diag.h"
#include "deltatime.h"
#include "timescale.h"

diag_t ttodeltatime(const char *t, deltatime_t *d, const struct timescale *default_scale)
{
	*d = deltatime_zero;

	uintmax_t time;
	shunk_t cursor = shunk1(t);

	/* <NUMBER>[<SCALE>] => TIME + [<SCALE>] */
	err_t err = shunk_to_uintmax(cursor, &cursor, 10/*base*/, &time);
	if (err != NULL) {
		return diag("bad duration value \"%s\": %s", t, err);
	}

	/* [<SCALE>] */
	const struct timescale *scale = ttotimescale(cursor, default_scale);
	if (scale == NULL) {
		return diag("unrecognized duration multiplier \""PRI_SHUNK"\"",
			    pri_shunk(cursor));
	}

	/* check convertint TIME to MS doesn't overflow */
	if (UINTMAX_MAX / scale->ms < time) {
		return diag("duration too large: \"%s\" is more than %ju seconds",
			    t, UINTMAX_MAX);
	}

	*d = deltatime_ms(time * scale->ms);
	return NULL;
}
