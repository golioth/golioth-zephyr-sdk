/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>

#include "coap_utils.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lightdb, CONFIG_GOLIOTH_LOG_LEVEL);

int golioth_lightdb_get(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			struct coap_reply *reply, coap_reply_t reply_cb)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (!reply || !reply_cb) {
		return -EINVAL;
	}

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT,
				     format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &packet);
	reply->reply = reply_cb;

	return golioth_send_coap(client, &packet);
}

int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			uint8_t *data, uint16_t data_len)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_POST, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_CONTENT_FORMAT,
				     format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	return golioth_send_coap_payload(client, &packet, data, data_len);
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
