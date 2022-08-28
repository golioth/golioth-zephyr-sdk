/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(stream, CONFIG_GOLIOTH_LOG_LEVEL);

#define STREAM_PATH		".s"

int golioth_stream_push_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   const uint8_t *data, size_t data_len,
			   golioth_req_cb_t cb, void *user_data)
{
	return golioth_coap_req_cb(client, COAP_METHOD_POST,
				   PATHV(STREAM_PATH, path), format,
				   data, data_len,
				   cb, user_data,
				   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

int golioth_stream_push(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			const uint8_t *data, size_t data_len)
{
	return golioth_coap_req_sync(client, COAP_METHOD_POST,
				     PATHV(STREAM_PATH, path), format,
				     data, data_len,
				     NULL, NULL,
				     GOLIOTH_COAP_REQ_NO_RESP_BODY);
}
