/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_utils.h"

#include <zephyr/net/coap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#define COAP_BASIC_HEADER_SIZE	4

static inline uint8_t coap_data_get_token_len(uint8_t *data)
{
	return data[0] & 0x0f;
}

static inline uint8_t coap_data_get_type(uint8_t *data)
{
	return (data[0] & 0x30) >> 4;
}

static inline uint8_t coap_data_get_code(uint8_t *data)
{
	return data[1];
}

int coap_data_check_rx_packet_type(uint8_t *data, size_t len)
{
	uint8_t tkl;

	if (!data) {
		return -EINVAL;
	}

	if (len < COAP_BASIC_HEADER_SIZE) {
		return -EINVAL;
	}

	/* Token lengths 9-15 are reserved. */
	tkl = coap_data_get_token_len(data);
	if (tkl > 8) {
		LOG_DBG("Invalid RX");
		return -EINVAL;
	}

	if (tkl == 0 &&
	    len == COAP_BASIC_HEADER_SIZE &&
	    coap_data_get_type(data) == COAP_TYPE_CON &&
	    coap_data_get_code(data) == COAP_CODE_EMPTY) {
		/* Empty packet */
		LOG_DBG("RX Empty");
		return -ENOMSG;
	}

	LOG_DBG("RX Non-empty");

	return 0;
}

int coap_packet_append_uri_path_from_string_range(struct coap_packet *packet,
						  const char *begin,
						  const char *end)
{
	const char *p;
	int err;

	/* Trim preceding '/' */
	while (*begin == '/') {
		begin++;
	}

	p = begin;

	/* Split path into URI-Path options */
	while (p < end && *p != '\0') {
		if (*p == '/') {
			err = coap_packet_append_option(packet,
							COAP_OPTION_URI_PATH,
							begin, p - begin);
			if (err) {
				return err;
			}

			begin = p + 1;
		}

		p++;
	}

	/* Return early if the last segment is empty */
	if (begin == p) {
		return 0;
	}

	/* Append last segment */
	return coap_packet_append_option(packet,
					 COAP_OPTION_URI_PATH,
					 begin, p - begin);
}
