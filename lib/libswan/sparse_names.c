/*
 * tables of names for values defined in constants.h
 * Copyright (C) 2012-2017 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2012 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 1998-2002,2015  D. Hugh Redelmeier.
 * Copyright (C) 2016-2017 Andrew Cagney
 * Copyright (C) 2017 Vukasin Karadzic <vukasin.karadzic@gmail.com>
 * Copyright (C) 2019 Andrew Cagney <cagney@gnu.org>
 * Copyright (C) 2020 Yulia Kuzovkova <ukuzovkova@gmail.com>
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

#include "sparse_names.h"

#include "jambuf.h"

/* look up enum names in a sparse_names */
const char *sparse_name(sparse_names sd, unsigned long val)
{
	for (const struct sparse_name *p = sd; p->name != NULL; p++) {
		if (p->value == val) {
			return p->name;
		}
	}

	return NULL;
}

/*
 * find or construct a string to describe an sparse value
 */
size_t jam_sparse(struct jambuf *buf, sparse_names sd, unsigned long val)
{
	const char *p = sparse_name(sd, val);
	if (p != NULL) {
		return jam_string(buf, p);
	}

	return jam(buf, "%lu??", val);
}

const char *str_sparse(sparse_names sd, unsigned long val, sparse_buf *buf)
{
	const char *p = sparse_name(sd, val);
	if (p != NULL) {
		return p;
	}

	snprintf(buf->buf, sizeof(buf->buf), "%lu??", val);
	return buf->buf;
}

const char *sparse_sparse_name(sparse_sparse_names ssd, unsigned long v1, unsigned long v2)
{
	while (ssd->names != NULL) {
		if (ssd->value == v1) {
			return sparse_name(ssd->names, v2);
		}
		ssd++;
	}
	return NULL;
}
