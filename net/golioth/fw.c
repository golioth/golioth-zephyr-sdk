/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <net/golioth/fw.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <stdio.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_FW_DOWNLOAD	".u"
#define GOLIOTH_FW_DESIRED	".u/desired"

#define GOLIOTH_FW_REPORT_STATE_URI_BASE	".u/c/"

enum {
	MANIFEST_KEY_SEQUENCE_NUMBER = 1,
	MANIFEST_KEY_HASH = 2,
	MANIFEST_KEY_COMPONENTS = 3,
};

enum {
	COMPONENT_KEY_PACKAGE = 1,
	COMPONENT_KEY_VERSION = 2,
	COMPONENT_KEY_HASH = 3,
	COMPONENT_KEY_SIZE = 4,
	COMPONENT_KEY_URI = 5,
};

static int parse_component(QCBORDecodeContext *decode_ctx,
			   uint8_t *version, size_t *version_len,
			   uint8_t *uri, size_t *uri_len)
{
	QCBORError qerr;
	UsefulBufC version_bufc;
	UsefulBufC uri_bufc;
	int err = 0;

	QCBORDecode_EnterMap(decode_ctx, NULL);

	QCBORDecode_GetTextStringInMapN(decode_ctx, COMPONENT_KEY_VERSION,
					&version_bufc);

	QCBORDecode_GetTextStringInMapN(decode_ctx, COMPONENT_KEY_URI,
					&uri_bufc);

	qerr = QCBORDecode_GetError(decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to get version and URI: %d (%s)", qerr, qcbor_err_to_str(qerr));
		err = -EINVAL;
		goto exit_map;
	}

	if (version_bufc.len > *version_len) {
		return -ENOMEM;
	}

	memcpy(version, version_bufc.ptr, version_bufc.len);
	*version_len = version_bufc.len;

	if (uri_bufc.len > *uri_len) {
		return -ENOMEM;
	}

	memcpy(uri, uri_bufc.ptr, uri_bufc.len);
	*uri_len = uri_bufc.len;

exit_map:
	QCBORDecode_ExitMap(decode_ctx);

	return err;
}

int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len,
			     uint8_t *uri, size_t *uri_len)
{
	int64_t manifest_sequence_number;
	UsefulBufC payload_bufc = { payload, payload_len };
	QCBORDecodeContext decode_ctx;
	QCBORError qerr;
	int err = 0;

	QCBORDecode_Init(&decode_ctx, payload_bufc, QCBOR_DECODE_MODE_NORMAL);

	QCBORDecode_EnterMap(&decode_ctx, NULL);

	QCBORDecode_GetInt64InMapN(&decode_ctx, MANIFEST_KEY_SEQUENCE_NUMBER,
				   &manifest_sequence_number);

	qerr = QCBORDecode_GetError(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to get manifest bstr: %d (%s)", qerr, qcbor_err_to_str(qerr));
		err = -EINVAL;
		goto exit_root_map;
	}

	LOG_INF("Manifest sequence-number: %d", (int) manifest_sequence_number);

	QCBORDecode_EnterArrayFromMapN(&decode_ctx, MANIFEST_KEY_COMPONENTS);

	err = parse_component(&decode_ctx, version, version_len, uri, uri_len);

	QCBORDecode_ExitArray(&decode_ctx);

	if (err) {
		goto exit_root_map;
	}

	qerr = QCBORDecode_GetError(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Error at the end: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return -EINVAL;
	}

exit_root_map:
	QCBORDecode_ExitMap(&decode_ctx);

	return err;
}

int golioth_fw_observe_desired(struct golioth_client *client,
			       struct coap_reply *reply,
			       coap_reply_t desired_cb)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + 2];
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
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + sizeof(ctx->uri)];
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
					ctx->uri, ctx->uri_len);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
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
			const char *uri, size_t uri_len,
			struct coap_reply *reply,
			golioth_blockwise_download_received_t received_cb)
{
	if (uri_len > sizeof(ctx->uri)) {
		return -ENOMEM;
	}

	memcpy(ctx->uri, uri, uri_len);
	ctx->uri_len = uri_len;

	golioth_blockwise_download_init(client, &ctx->blockwise_ctx);

	coap_reply_clear(reply);
	reply->reply = golioth_fw_block_received;
	reply->user_data = ctx;
	ctx->blockwise_ctx.reply = reply;
	ctx->blockwise_ctx.received_cb = received_cb;

	return golioth_fw_download_next(ctx);
}

int golioth_fw_report_state(struct golioth_client *client,
			    const char *package_name,
			    enum golioth_fw_state state,
			    enum golioth_dfu_result result)
{
	struct coap_packet packet;
	uint8_t uri[sizeof(GOLIOTH_FW_REPORT_STATE_URI_BASE) +
		    CONFIG_GOLIOTH_FW_PACKAGE_NAME_MAX_LEN];
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + 32 + sizeof(uri)];
	QCBOREncodeContext encode_ctx;
	UsefulBuf encode_bufc;
	size_t encoded_len;
	int written;
	int err;
	QCBORError qerr;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_POST, coap_next_id());
	if (err) {
		return err;
	}

	written = snprintf(uri, sizeof(uri), GOLIOTH_FW_REPORT_STATE_URI_BASE "%s",
			   package_name);
	if (written >= sizeof(uri)) {
		LOG_ERR("not enough space in URI buffer");
		return -ENOMEM;
	}

	err = coap_packet_append_option(&packet, COAP_OPTION_URI_PATH,
					uri, written);
	if (err) {
		LOG_ERR("failed to append logs uri path: %d", err);
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_CONTENT_FORMAT,
				     COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		LOG_ERR("failed to append logs content format: %d", err);
		return err;
	}

	err = coap_packet_append_payload_marker(&packet);
	if (err) {
		LOG_ERR("failed to append logs payload marker: %d", err);
		return err;
	}

	encode_bufc.ptr = &buffer[packet.offset];
	encode_bufc.len = sizeof(buffer) - packet.offset;

	QCBOREncode_Init(&encode_ctx, encode_bufc);

	QCBOREncode_OpenMap(&encode_ctx);

	QCBOREncode_AddUInt64ToMap(&encode_ctx, "s", state);
	QCBOREncode_AddUInt64ToMap(&encode_ctx, "r", result);

	QCBOREncode_CloseMap(&encode_ctx);

	qerr = QCBOREncode_FinishGetSize(&encode_ctx, &encoded_len);
	if (qerr != QCBOR_SUCCESS) {
		/* TODO: better error mapping */
		return -EINVAL;
	}

	/*
	 * Add CBOR payload into CoAP payload. In fact CBOR is already in good
	 * place in memory, so the only thing that is needed is moving forward
	 * CoAP offset.
	 *
	 * TODO: add CoAP API that will prevent internal memcpy()
	 */
	err = coap_packet_append_payload(&packet, &buffer[packet.offset],
					 encoded_len);
	if (err) {
		return err;
	}

	return golioth_send_coap(client, &packet);
}
