/*
 * Copyright (c) 2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zcbor_decode.h>

/** Extract the major type, i.e. the first 3 bits of the header byte. */
#define ZCBOR_MAJOR_TYPE(header_byte) ((zcbor_major_type_t)(((header_byte) >> 5) & 0x7))

enum {
	ZCBOR_MAP_KEY_TYPE_U32,
	ZCBOR_MAP_KEY_TYPE_TSTR,
};

struct zcbor_map_key {
	uint8_t type;
	union {
		uint32_t u32;
		struct zcbor_string tstr;
	};
};

struct zcbor_map_entry {
	struct zcbor_map_key key;
	int (*decode)(zcbor_state_t *zsd, void *value);
	void *value;
};

static inline bool zcbor_list_or_map_end(zcbor_state_t *state)
{
	if (state->indefinite_length_array) {
		return *state->payload == 0xff;
	} else {
		return state->elem_count == 0;
	}

	return false;
}

int zcbor_map_int64_decode(zcbor_state_t *zsd, void *value);

int zcbor_map_tstr_decode(zcbor_state_t *zsd, void *value);

int zcbor_map_decode(zcbor_state_t *zsd,
		     struct zcbor_map_entry *entries,
		     size_t num_entries);

#define ZCBOR_U32_MAP_ENTRY(_u32, _decode, _value)			\
	{								\
		.key = {						\
			.type = ZCBOR_MAP_KEY_TYPE_U32,			\
			.u32 = _u32,					\
		},							\
		.decode = _decode,					\
		.value = _value,					\
	}

#define ZCBOR_TSTR_LIT_MAP_ENTRY(_tstr_lit, _decode, _value)		\
	{								\
		.key = {						\
			.type = ZCBOR_MAP_KEY_TYPE_TSTR,		\
			.tstr = {_tstr_lit, sizeof(_tstr_lit) - 1},	\
		},							\
		.decode = _decode,					\
		.value = _value,					\
	}
