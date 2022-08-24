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

/**
 * Check CoAP packet type based on raw data received.
 *
 * @retval -EINVAL invalid message
 * @retval -ENOMSG empty CoAP message (ping)
 * @retval 0 valid CoAP packet (to be parsed with)
 */
int coap_data_check_rx_packet_type(uint8_t *data, size_t len);

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
