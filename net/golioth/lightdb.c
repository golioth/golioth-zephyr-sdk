/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lightdb, CONFIG_GOLIOTH_LOG_LEVEL);

#define LIGHTDB_PATH		".d"

static inline int golioth_coap_req_lightdb_cb(struct golioth_client *client,
					      enum coap_method method,
					      const uint8_t *path,
					      enum golioth_content_format format,
					      const uint8_t *data, size_t data_len,
					      golioth_req_cb_t cb, void *user_data,
					      int flags)
{
	return golioth_coap_req_cb(client, method, PATHV(LIGHTDB_PATH, path), format,
				   data, data_len,
				   cb, user_data,
				   flags);
}

static inline int golioth_coap_req_lightdb_sync(struct golioth_client *client,
						enum coap_method method,
						const uint8_t *path,
						enum golioth_content_format format,
						const uint8_t *data, size_t data_len,
						golioth_req_cb_t cb, void *user_data,
						int flags)
{
	return golioth_coap_req_sync(client, method, PATHV(LIGHTDB_PATH, path), format,
				     data, data_len,
				     cb, user_data,
				     flags);
}

int golioth_lightdb_get_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   golioth_req_cb_t cb, void *user_data)
{
	return golioth_coap_req_lightdb_cb(client, COAP_METHOD_GET, path, format,
					   NULL, 0,
					   cb, user_data,
					   0);
}

struct golioth_lightdb_get_prealloc_data {
	uint8_t *data;
	size_t capacity;
	size_t len;
};

static int golioth_lightdb_get_prealloc_cb(struct golioth_req_rsp *rsp)
{
	struct golioth_lightdb_get_prealloc_data *prealloc_data = rsp->user_data;
	size_t total = rsp->total;

	/*
	 * In case total length is not known, just take into account length of already received
	 * data.
	 */
	if (!total) {
		total = rsp->off + rsp->len;
	}

	/* Make sure that preallocated buffer has enough capacity to store whole response. */
	if (prealloc_data->capacity < total) {
		LOG_WRN("Not enough capacity in buffer (%zu < %zu)",
			prealloc_data->capacity, total);
		return -ENOSPC;
	}

	prealloc_data->len = total;

	memcpy(&prealloc_data->data[rsp->off], rsp->data, rsp->len);

	return 0;
}

int golioth_lightdb_get(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			uint8_t *data, size_t *len)
{
	struct golioth_lightdb_get_prealloc_data prealloc_data = {
		.data = data,
		.capacity = *len,
	};
	int err;

	err = golioth_coap_req_lightdb_sync(client, COAP_METHOD_GET, path, format,
					    NULL, 0,
					    golioth_lightdb_get_prealloc_cb,
					    &prealloc_data,
					    0);
	if (err) {
		return err;
	}

	*len = prealloc_data.len;

	return 0;
}

int golioth_lightdb_set_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   const uint8_t *data, size_t data_len,
			   golioth_req_cb_t cb, void *user_data)
{
	return golioth_coap_req_lightdb_cb(client, COAP_METHOD_POST, path, format,
					   data, data_len,
					   cb, user_data,
					   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			const uint8_t *data, size_t data_len)
{
	return golioth_coap_req_lightdb_sync(client, COAP_METHOD_POST, path, format,
					     data, data_len,
					     NULL, NULL,
					     GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

static int golioth_coap_observe_init(struct coap_packet *packet,
				     uint8_t *buffer, size_t buffer_len,
				     const char *path)
{
	int err;

	err = coap_packet_init(packet, buffer, buffer_len,
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_append_option_int(packet, COAP_OPTION_OBSERVE, 0);
	if (err) {
		LOG_ERR("Unable to add observe option");
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	return 0;
}

int golioth_lightdb_observe(struct golioth_client *client, const uint8_t *path,
			    enum golioth_content_format format,
			    struct coap_reply *reply, coap_reply_t reply_cb)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (!reply || !reply_cb) {
		return -EINVAL;
	}

	err = golioth_coap_observe_init(&packet, buffer, sizeof(buffer), path);
	if (err) {
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT, format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &packet);
	reply->reply = reply_cb;

	return golioth_send_coap(client, &packet);
}

int golioth_lightdb_delete(struct golioth_client *client, const uint8_t *path)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_DELETE, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_option(&packet, COAP_OPTION_URI_PATH,
					path, strlen(path));
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	return golioth_send_coap(client, &packet);
}
