/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <net/golioth/fw.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_FW_DOWNLOAD	".u"
#define GOLIOTH_FW_DESIRED	".u/desired"

int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len)
{
	struct cbor_buf_reader reader;
	CborParser parser;
	CborValue value;
	CborValue target;
	CborValue v;
	CborError err;

	cbor_buf_reader_init(&reader, payload, payload_len);

	err = cbor_parser_init(&reader.r, 0, &parser, &value);
	if (err != CborNoError) {
		LOG_ERR("Failed to init CBOR parser: %d", err);
		return -EINVAL;
	}

	if (!cbor_value_is_map(&value)) {
		LOG_ERR("Received value is not a map!");
		return -EINVAL;
	}

	err = cbor_value_map_find_value(&value, "t", &target);
	if (err != CborNoError) {
		LOG_ERR("Failed to find 't' key");
		return -EINVAL;
	}

	if (cbor_value_get_type(&target) == CborInvalidType) {
		LOG_WRN("No target firmware information");
		return -ENOENT;
	}

	if (!cbor_value_is_map(&target)) {
		LOG_ERR("Received target value is not a map!");
		return -EINVAL;
	}

	err = cbor_value_map_find_value(&target, "v", &v);
	if (err != CborNoError) {
		LOG_ERR("Received target map does not contain 'v' key");
		return -EINVAL;
	}

	if (!cbor_value_is_text_string(&v)) {
		LOG_ERR("Received version is not a text string");
		return -EINVAL;
	}

	err = cbor_value_copy_text_string(&v, version, version_len, NULL);
	if (err != CborNoError) {
		LOG_ERR("Failed to copy version string");
		return -ENOMEM;
	}

	return 0;
}

int golioth_fw_observe_desired(struct golioth_client *client,
			       const char *current_version,
			       struct coap_reply *reply,
			       coap_reply_t desired_cb)
{
	struct coap_packet packet;
	char version_query[64];
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + 2 + sizeof(version_query)];
	int written;
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_OBSERVE, 0);
	if (err) {
		LOG_ERR("Unable to add observe option");
		return err;
	}

	err = coap_packet_append_option(&packet, COAP_OPTION_URI_PATH,
					GOLIOTH_FW_DESIRED,
					sizeof(GOLIOTH_FW_DESIRED) - 1);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	written = snprintf(version_query, sizeof(version_query), "v=%s",
			   current_version);
	if (written >= sizeof(version_query)) {
		return -ENOMEM;
	}

	err = coap_packet_append_option(&packet, COAP_OPTION_URI_QUERY,
					version_query, strlen(version_query));
	if (err) {
		LOG_ERR("Unable add uri query to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT,
				     COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &packet);
	reply->reply = desired_cb;
	reply->user_data = client;

	return golioth_send_coap(client, &packet);
}

static int golioth_fw_download_next(struct golioth_fw_download_ctx *ctx)
{
	struct golioth_blockwise_download_ctx *blockwise_ctx = &ctx->blockwise_ctx;
	struct golioth_client *client = blockwise_ctx->client;
	struct coap_block_context *block_ctx = &blockwise_ctx->block_ctx;
	struct coap_packet request;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + 64 /* size of version string */];
	int err;

	err = coap_packet_init(&request, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       sizeof(blockwise_ctx->token),
			       blockwise_ctx->token,
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		LOG_ERR("Failed to init update block request: %d", err);
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					GOLIOTH_FW_DOWNLOAD,
					sizeof(GOLIOTH_FW_DOWNLOAD) - 1);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_QUERY,
					ctx->version_query,
					strlen(ctx->version_query));
	if (err) {
		LOG_ERR("Unable add uri query to packet");
		return err;
	}

	err = coap_append_block2_option(&request, block_ctx);
	if (err) {
		LOG_ERR("Failed to append block2: %d", err);
		return err;
	}

	LOG_DBG("Request block %d",
		(int) (block_ctx->current /
		       coap_block_size_to_bytes(block_ctx->block_size)));

	/*
	 * TODO: coap reply should not be used between requests, due to
	 * synchronization issues (possibility of using the same reply from
	 * multiple threads).
	 */
	coap_reply_init(blockwise_ctx->reply, &request);

	return golioth_send_coap(client, &request);
}

static int golioth_fw_block_received(const struct coap_packet *update,
				     struct coap_reply *reply,
				     const struct sockaddr *from)
{
	struct golioth_fw_download_ctx *ctx = reply->user_data;
	struct golioth_blockwise_download_ctx *blockwise_ctx = &ctx->blockwise_ctx;
	struct golioth_client *client = blockwise_ctx->client;
	struct coap_block_context *block_ctx = &blockwise_ctx->block_ctx;
	const uint8_t *payload;
	uint16_t payload_len;
	bool truncated = (client->rx_received > client->rx_buffer_len);
	size_t cur_offset, new_offset;
	int err;

	LOG_DBG("Update on blockwise download");

	payload = coap_packet_get_payload(update, &payload_len);
	if (!payload) {
		LOG_ERR("No payload in CoAP!");
		return -EIO;
	}

	if (truncated) {
		LOG_ERR("Payload is truncated!");
		return -EIO;
	}

	err = coap_update_from_block(update, block_ctx);
	if (err) {
		LOG_WRN("Failed to update update block context (%d)", err);
		return err;
	}

	if (!block_ctx->total_size) {
		LOG_DBG("Not a blockwise packet");
		blockwise_ctx->received_cb(blockwise_ctx, payload, 0, payload_len, true);
		return 0;
	}

	cur_offset = block_ctx->current;
	new_offset = coap_next_block(update, block_ctx);

	if (new_offset == 0) {
		LOG_DBG("Blockwise transfer is finished!");
		blockwise_ctx->received_cb(blockwise_ctx, payload, cur_offset, payload_len, true);
		return 0;
	}

	LOG_DBG("Update offset: %zu -> %zu", cur_offset, new_offset);

	err = blockwise_ctx->received_cb(blockwise_ctx, payload, cur_offset, payload_len, false);
	if (err) {
		return err;
	}

	return golioth_fw_download_next(ctx);
}

int golioth_fw_download(struct golioth_client *client,
			struct golioth_fw_download_ctx *ctx,
			const char *version, size_t version_len,
			struct coap_reply *reply,
			golioth_blockwise_download_received_t received_cb)
{
	int written;

	written = snprintf(ctx->version_query, sizeof(ctx->version_query),
			   "v=%.*s", version_len, version);
	if (written >= sizeof(ctx->version_query)) {
		return -ENOMEM;
	}

	golioth_blockwise_download_init(client, &ctx->blockwise_ctx);

	coap_reply_clear(reply);
	reply->reply = golioth_fw_block_received;
	reply->user_data = ctx;
	ctx->blockwise_ctx.reply = reply;
	ctx->blockwise_ctx.received_cb = received_cb;

	return golioth_fw_download_next(ctx);
}
