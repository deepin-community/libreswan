/* Output raw bytes, for libreswan
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

#include "lswlog.h"
#include "chunk.h"
#include "ttodata.h"	/* for datatot() */
#include "passert.h"

void llog_base64_bytes(lset_t rc_flags,
		       const struct logger *logger,
		       const void *ptr, size_t size)
{
	/*
	 * A byte is 8-bits, base64 uses 6-bits (2^6=64).  Plus some
	 * for \0.  Plus some extra for the trailing === and rounding.
	 */
	chunk_t base64 = alloc_chunk(size * 8 / 6 + 1 + 10, "base64");
	size_t base64_size = datatot(ptr, size, 64, (void*)base64.ptr, base64.len);
	/* BASE64_SIZE includes '\0' */
	passert(base64_size <= base64.len);
	passert(base64_size > 0);
	shunk_t rest = shunk2(base64.ptr, base64_size - 1);
	while (true) {
		shunk_t line = shunk_slice(rest, 0, min((size_t)64, rest.len));
		if (line.len == 0) break;
		rest = shunk_slice(rest, line.len, rest.len);
		llog(rc_flags, logger, PRI_SHUNK, pri_shunk(line));
	}
	free_chunk_content(&base64);
}
