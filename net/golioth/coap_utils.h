/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NET_GOLIOTH_COAP_UTILS_H__
#define __NET_GOLIOTH_COAP_UTILS_H__

#include <stddef.h>
#include <stdint.h>

struct coap_packet;

int coap_packet_append_uri_path_from_string_range(struct coap_packet *packet,
						  const char *begin, const char *end);

static inline int coap_packet_append_uri_path_from_string(struct coap_packet *packet,
							  const char *path, size_t path_len)
{
	return coap_packet_append_uri_path_from_string_range(packet, path, path + path_len);
}

static inline int coap_packet_append_uri_path_from_stringz(struct coap_packet *packet,
							   const char *path)
{
	return coap_packet_append_uri_path_from_string_range(packet, path, (void *)UINTPTR_MAX);
}

#endif /* __NET_GOLIOTH_COAP_UTILS_H__ */
