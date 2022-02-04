/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_utils.h"

#include <net/coap.h>

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
