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

/*
 * Note that the array sizes are all specified; this is to enable range
 * checking by code that only includes constants.h.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <netinet/in.h>

#include <ietf_constants.h>
#include "passert.h"

#include "constants.h"
#include "enum_names.h"
#include "lswlog.h"
#include "ip_said.h"		/* for SPI_PASS et.al. */
#include "secrets.h"		/* for enum privae_key_kind */

/*
 * Iterate over the enum_names returning all the valid indexes.
 *
 * Use -1 as the starting point / sentinel.
 *
 * XXX: Works fine provided we ignore the enum_names object that
 * contains negative values stored in unsigned fields!
 */

long next_enum(enum_names *en, long l)
{
	enum_names *p = en;
	unsigned long e;
	if (l < 0) {
		e = en->en_first;
		if (en->en_names[e - p->en_first] != NULL) {
			return e;
		}
	} else {
		e = l;
	}

	while (true) {
		while (true) {
			if (p == NULL) {
				return -1;
			}
			passert(p->en_last - p->en_first + 1 == p->en_checklen);
			if (p->en_first <= e && e < p->en_last) {
				e++;
				break;
			} else if (e == p->en_last && p->en_next_range != NULL) {
				p = p->en_next_range;
				e = p->en_first;
				break;
			} else {
				p = p->en_next_range;
			}
		}
		if (p->en_names[e - p->en_first] != NULL) {
			return e;
		}
	}
}

/*
 * the enum_name range containing VAL, or NULL.
 */
const struct enum_names *enum_range(const struct enum_names *en, unsigned long val, const char **prefix)
{
	*prefix = NULL;
	for (enum_names *p = en; p != NULL; p = p->en_next_range) {
		passert(p->en_last - p->en_first + 1 == p->en_checklen);
		/* return most recent prefix */
		if (p->en_prefix != NULL) {
			*prefix = p->en_prefix;
		}
		if (p->en_first <= val && val <= p->en_last) {
			return p;
		}
	}
	return NULL;
}

/*
 * The actual name for VAL, using RANGE; possibly shortened using
 * PREFIX.
 */
const char *enum_range_name(const struct enum_names *range, unsigned long val,
			    const char *prefix, bool shorten)
{
	if (range == NULL) {
		return NULL;
	}
	passert(range->en_first <= val && val <= range->en_last);
	/* can be NULL */
	const char *name = range->en_names[val - range->en_first];
	if (name != NULL && prefix != NULL && shorten) {
		/* grr: can't use eat() */
		size_t pl = strlen(prefix);
		return strneq(name, prefix, pl) ? name + pl : name;
	} else {
		return name;
	}
}

/* look up enum names in an enum_names */
const char *enum_name(enum_names *ed, unsigned long val)
{
	const char *prefix = NULL;
	/* can be NULL */
	const struct enum_names *range = enum_range(ed, val, &prefix);
	/* can be NULL */
	return enum_range_name(range, val, prefix, /*shorten?*/false);
}

const char *enum_name_short(enum_names *ed, unsigned long val)
{
	const char *prefix = NULL;
	/* can be NULL */
	const struct enum_names *range = enum_range(ed, val, &prefix);
	/* can be NULL */
	return enum_range_name(range, val, prefix, /*shorten?*/true);
}

size_t jam_enum(struct jambuf *buf, enum_names *en, unsigned long val)
{
	const char *name = enum_name(en, val);
	if (name == NULL) {
		if (en->en_prefix != NULL) {
			jam_string(buf, en->en_prefix);
			jam_string(buf, "_");
		}
		return jam(buf, "%lu??", val);
	}
	return jam_string(buf, name);
}

size_t jam_enum_short(struct jambuf *buf, enum_names *en, unsigned long val)
{
	const char *name = enum_name_short(en, val);
	if (name == NULL) {
		if (en->en_prefix != NULL) {
			jam_string(buf, en->en_prefix);
			jam_string(buf, "_");
		}
		return jam(buf, "%lu??", val);
	}
	return jam_string(buf, name);
}

/*
 * find or construct a string to describe an enum value
 *
 * Note: result may or may not be in b.
 */
const char *str_enum(enum_names *ed, unsigned long val, enum_buf *b)
{
	const char *p = enum_name(ed, val);

	if (p == NULL) {
		snprintf(b->buf, sizeof(b->buf), "%lu??", val);
		p = b->buf;
	}
	return p;
}

const char *str_enum_short(enum_names *ed, unsigned long val, enum_buf *b)
{
	const char *prefix;
	const struct enum_names *range = enum_range(ed, val, &prefix);
	/* could be NULL */
	const char *name = enum_range_name(range, val, prefix, /*shorten?*/true);
	if (name == NULL) {
		snprintf(b->buf, sizeof(b->buf), "%lu??", val);
		name = b->buf;
	}
	return name;
}

/*
 * Find the value for a name in an enum_names table.  If not found, returns -1.
 *
 * Strings are compared without regard to case.
 *
 * ??? the table contains unsigned long values BUT the function returns an
 * int so there is some potential for overflow.
 */
int enum_search(enum_names *ed, const char *str)
{
	for (enum_names *p = ed; p != NULL; p = p->en_next_range) {
		passert(p->en_last - p->en_first + 1 == p->en_checklen);
		for (unsigned long en = p->en_first; en <= p->en_last; en++) {
			const char *ptr = p->en_names[en - p->en_first];

			if (ptr != NULL && strcaseeq(ptr, str)) {
				passert(en <= INT_MAX);
				return en;
			}
		}
	}
	return -1;
}

int enum_match(enum_names *ed, shunk_t string)
{
	const char *prefix = NULL;
	for (enum_names *p = ed; p != NULL; p = p->en_next_range) {
		passert(p->en_last - p->en_first + 1 == p->en_checklen);
		prefix = (p->en_prefix == NULL ? prefix : p->en_prefix);
		for (unsigned long en = p->en_first; en <= p->en_last; en++) {
			const char *name = p->en_names[en - p->en_first];

			if (name == NULL) {
				continue;
			}

			passert(en <= INT_MAX);

			/*
			 * try matching all four variants of name:
			 * with and without prefix en->en_prefix and
			 * with and without suffix '(...)'
			 */
			size_t name_len = strlen(name);
			size_t prefix_len = (prefix == NULL ? 0 : strlen(prefix));

			/* suffix must not and will not overlap prefix */
			const char *suffix = strchr(name + prefix_len, '(');

			size_t suffix_len = (suffix != NULL && name[name_len - 1] == ')' ?
					     &name[name_len] - suffix : 0);

#			define try(guard, f, b) ( \
				(guard) && \
				name_len - ((f) + (b)) == string.len && \
				strncaseeq(name + (f), string.ptr, string.len))

			if (try(true, 0, 0) ||
			    try(suffix_len > 0, 0, suffix_len) ||
			    try(prefix_len > 0, prefix_len, 0) ||
			    try(prefix_len > 0 && suffix_len > 0, prefix_len, suffix_len))
			{
				return en;
			}
#			undef try
		}
	}
	return -1;
}

/* choose table from struct enum_enum_names */
enum_names *enum_enum_table(enum_enum_names *een,
			    unsigned long table)
{
	passert(een->een_last - een->een_first + 1 == een->een_checklen);

	if (een->een_first <= table && table <= een->een_last) {
		return een->een_enum_name[table - een->een_first];
	} else {
		return NULL;
	}
}

const char *enum_enum_name(enum_enum_names *een, unsigned long table,
			   unsigned long val)
{
	enum_names *en = enum_enum_table(een, table);

	return en == NULL ? NULL : enum_name(en, val);
}

const char *str_enum_enum(enum_enum_names *een, unsigned long table,
			  unsigned long val, enum_buf *b)
{
	enum_names *en = enum_enum_table(een, table);
	if (en == NULL) {
		/* assume the log context implies the table name */
		snprintf(b->buf, sizeof(b->buf), "%lu??", val);
		return b->buf;
	}

	return str_enum(en, val, b);
}

const char *str_enum_enum_short(enum_enum_names *een, unsigned long table,
				unsigned long val, enum_buf *b)
{
	enum_names *en = enum_enum_table(een, table);
	if (en == NULL) {
		/* assume the log context implies the table name */
		snprintf(b->buf, sizeof(b->buf), "%lu??", val);
		return b->buf;
	}

	return str_enum_short(en, val, b);
}

size_t jam_enum_enum(struct jambuf *buf, enum_enum_names *een,
		     unsigned long table, unsigned long val)
{
	enum_names *en = enum_enum_table(een, table);
	if (en == NULL) {
		/* XXX: dump something more meaningful */
		return jam(buf, "%lu??%lu??", table, val);
	}
	return jam_enum(buf, en, val);
}

size_t jam_enum_enum_short(struct jambuf *buf, enum_enum_names *een,
			   unsigned long table, unsigned long val)
{
	enum_names *en = enum_enum_table(een, table);
	if (en == NULL) {
		/* XXX: dump something more meaningful */
		return jam(buf, "%lu??%lu??", table, val);
	}
	return jam_enum_short(buf, en, val);
}

void check_enum_names(enum_names *checklist[], size_t tl)
{
	/* check that enum_names are well-formed */
	size_t i;

	for (i = 0; i != tl; i++) {
		/*
		 * enum_name will check all linked enum_names
		 * if given a value that isn't covered.
		 * -42 is probably not covered.
		 */
		(void) enum_name(checklist[i], -42UL);
	}
}
