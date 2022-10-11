/* Simple ASN.1 parser, for libreswan
 *
 * Copyright (C) 2000-2004 Andreas Steffen, Zuercher Hochschule Winterthur
 * Copyright (C) 2005 Michael Richardson <mcr@marajade.sandelman.ca>
 * Copyright (C) 2009 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2013 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2013 Paul Wouters <pwouters@redhat.com>
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
 *
 */

#ifndef ASN1_H
#define ASN1_H

#include "realtime.h"
#include "chunk.h"

/* Defines some primitive ASN1 types */

enum asn1_type {
	ASN1_EOC =                  0x00,
	ASN1_BOOLEAN =              0x01,
	ASN1_INTEGER =              0x02,
	ASN1_BIT_STRING =           0x03,
	ASN1_OCTET_STRING =         0x04,
	ASN1_NULL =                 0x05,
	ASN1_OID =                  0x06,
	ASN1_ENUMERATED =           0x0A,
	ASN1_UTF8STRING =           0x0C,
	ASN1_NUMERICSTRING =        0x12,
	ASN1_PRINTABLESTRING =      0x13,
	ASN1_T61STRING =            0x14,
	ASN1_VIDEOTEXSTRING =       0x15,
	ASN1_IA5STRING =            0x16,
	ASN1_UTCTIME =              0x17,
	ASN1_GENERALIZEDTIME =      0x18,
	ASN1_GRAPHICSTRING =        0x19,
	ASN1_VISIBLESTRING =        0x1A,
	ASN1_GENERALSTRING =        0x1B,
	ASN1_UNIVERSALSTRING =      0x1C,
	ASN1_BMPSTRING =            0x1E,

	ASN1_CONSTRUCTED =          0x20,

	ASN1_SEQUENCE =             0x30,

	ASN1_SET =                  0x31,

	ASN1_CONTEXT_S_0 =          0x80,
	ASN1_CONTEXT_S_1 =          0x81,
	ASN1_CONTEXT_S_2 =          0x82,
	ASN1_CONTEXT_S_3 =          0x83,
	ASN1_CONTEXT_S_4 =          0x84,
	ASN1_CONTEXT_S_5 =          0x85,
	ASN1_CONTEXT_S_6 =          0x86,
	ASN1_CONTEXT_S_7 =          0x87,
	ASN1_CONTEXT_S_8 =          0x88,

	ASN1_CONTEXT_C_0 =          0xA0,
	ASN1_CONTEXT_C_1 =          0xA1,
	ASN1_CONTEXT_C_2 =          0xA2,
	ASN1_CONTEXT_C_3 =          0xA3,
	ASN1_CONTEXT_C_4 =          0xA4,
	ASN1_CONTEXT_C_5 =          0xA5
};

extern void code_asn1_length(size_t length, chunk_t *code);

bool is_asn1_printablestring(shunk_t str);

typedef shunk_t asn1_t;
#define ASN1(CHUNK)							\
	({								\
		chunk_t h_ = CHUNK; /* type assertion */		\
		asn1_t asn1_ = { .ptr = h_.ptr, .len = h_.len, };	\
		asn1_;							\
	})

int known_oid(asn1_t object);

err_t asn1_ok(asn1_t blob);

err_t unwrap_asn1_type(asn1_t *cursor, enum asn1_type *ty);
err_t unwrap_asn1_length(asn1_t *cursor, size_t *length);
err_t unwrap_asn1_value(asn1_t *cursor, size_t length, asn1_t *value);
/* type-length-value https://en.wikipedia.org/wiki/Type%E2%80%93length%E2%80%93value */
err_t unwrap_asn1_tlv(asn1_t *cursor, enum asn1_type ty, asn1_t *contents);

#endif
