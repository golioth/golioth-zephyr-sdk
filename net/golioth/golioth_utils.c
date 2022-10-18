/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#include "golioth_utils.h"

static enum coap_block_size max_block_size_from_payload_len(uint16_t payload_len)
{
	enum coap_block_size block_size = COAP_BLOCK_16;

	payload_len /= 16;

	while (payload_len > 1 && block_size < COAP_BLOCK_1024) {
		block_size++;
		payload_len /= 2;
	}

	return block_size;
}

enum coap_block_size golioth_estimated_coap_block_size(struct golioth_client *client)
{
	return max_block_size_from_payload_len(client->rx_buffer_len);
}

int golioth_req_rsp_default_handler(struct golioth_req_rsp *rsp)
{
	const char *info = rsp->user_data;

	if (rsp->err) {
		LOG_ERR("Error response (%s): %d", info ? info : "app", rsp->err);
		return 0;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, info ? info : "RSP");

	return 0;
}
