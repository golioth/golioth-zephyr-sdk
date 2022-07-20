/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/rpc.h>
#include <net/golioth/system_client.h>
#include <logging/log.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <stdio.h>

#include "coap_utils.h"

/*
 * Request:
 * {
 *      "id": "id_string,
 *      "method": "method_name_string",
 *      "params": [...]
 * }
 *
 * Response:
 * {
 *      "id": "id_string",
 *      "statusCode": integer,
 *      "detail": {...}
 * }
 */

LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_RPC_PATH ".rpc"
#define GOLIOTH_RPC_STATUS_PATH ".rpc/status"
#define GOLIOTH_RPC_MAX_RESPONSE_LEN 256

K_MUTEX_DEFINE(_rpc_mutex);

static int send_response(struct golioth_client *client,
			 uint8_t *coap_payload, size_t coap_payload_len)
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

	err = coap_packet_append_uri_path_from_stringz(&packet, GOLIOTH_RPC_STATUS_PATH);
	if (err) {
		LOG_ERR("Unable add uri path to packet: %d", err);
		return err;
	}

	err = coap_append_option_int(&packet,
				     COAP_OPTION_CONTENT_FORMAT,
				     COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		LOG_ERR("Unable add content format to packet: %d", err);
		return err;
	}

	err = golioth_send_coap_payload(client, &packet, coap_payload, coap_payload_len);
	if (err) {
		LOG_ERR("Error in golioth_send_coap: %d", err);
	}
	return err;
}

static int on_rpc(const struct coap_packet *response,
		  struct coap_reply *reply,
		  const struct sockaddr *from)
{
	struct golioth_client *client =
		CONTAINER_OF(reply, struct golioth_client, rpc.observe_reply);
	uint16_t payload_len;
	const uint8_t *payload = coap_packet_get_payload(response, &payload_len);

	LOG_HEXDUMP_INF(payload, payload_len, "Payload");

	if (payload_len == 3 && payload[1] == 'O' && payload[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	UsefulBufC payload_buf = { payload, payload_len };

	QCBORDecodeContext decode_ctx;
	QCBORError qerr;
	UsefulBufC id_buf;
	UsefulBufC method_buf;

	/* Decode id and method from request */
	QCBORDecode_Init(&decode_ctx, payload_buf, QCBOR_DECODE_MODE_NORMAL);
	QCBORDecode_EnterMap(&decode_ctx, NULL);
	QCBORDecode_GetTextStringInMapSZ(&decode_ctx, "id", &id_buf);
	QCBORDecode_GetTextStringInMapSZ(&decode_ctx, "method", &method_buf);
	qerr = QCBORDecode_GetError(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to parse RPC request: %d (%s)", qerr, qcbor_err_to_str(qerr));
		QCBORDecode_ExitMap(&decode_ctx);
		QCBORDecode_Finish(&decode_ctx);
		return qcbor_error_to_posix(qerr);
	}

	/* Start encoding response */
	uint8_t response_buf[GOLIOTH_RPC_MAX_RESPONSE_LEN];
	UsefulBuf response_bufc = { response_buf, sizeof(response_buf) };
	QCBOREncodeContext encode_ctx;

	QCBOREncode_Init(&encode_ctx, response_bufc);
	QCBOREncode_OpenMap(&encode_ctx);
	QCBOREncode_AddTextToMap(&encode_ctx, "id", id_buf);

	/* Search for matching RPC registration */
	const struct golioth_rpc_method *matching_method = NULL;

	k_mutex_lock(&_rpc_mutex, K_FOREVER);

	for (int i = 0; i < client->rpc.num_methods; i++) {
		const struct golioth_rpc_method *method = &client->rpc.methods[i];

		if (strncmp(method->name, method_buf.ptr, method_buf.len) == 0) {
			matching_method = method;
			break;
		}
	}

	enum golioth_rpc_status status_code = GOLIOTH_RPC_UNKNOWN;

	if (matching_method) {
		LOG_DBG("Calling registered RPC method: %s", matching_method->name);

		/**
		 * Call callback while decode context is inside the params array
		 * and encode context is inside the detail map.
		 */
		QCBORDecode_EnterArrayFromMapSZ(&decode_ctx, "params");
		QCBOREncode_OpenMapInMap(&encode_ctx, "detail");
		status_code = matching_method->callback(&decode_ctx,
							&encode_ctx,
							matching_method->callback_arg);
		QCBOREncode_CloseMap(&encode_ctx);
		QCBORDecode_ExitArray(&decode_ctx);
	}

	k_mutex_unlock(&_rpc_mutex);

	QCBOREncode_AddUInt64ToMap(&encode_ctx, "statusCode", status_code);

	QCBOREncode_CloseMap(&encode_ctx); /* root request map */
	QCBORDecode_ExitMap(&decode_ctx); /* root response map */

	/* Finalize decoding */
	QCBORDecode_Finish(&decode_ctx);

	/* Finalize encoding */
	size_t response_len;

	qerr = QCBOREncode_FinishGetSize(&encode_ctx, &response_len);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("QCBOREncode_FinishGetSize error: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return qcbor_error_to_posix(qerr);
	}

	LOG_HEXDUMP_DBG(response_buf, response_len, "Response");

	/* Send CoAP response packet */
	return send_response(client, response_buf, response_len);
}

static int golioth_rpc_observe(struct golioth_client *client)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		LOG_ERR("Failed to initialize packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_OBSERVE, 0);
	if (err) {
		LOG_ERR("Unable to add observe option");
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, GOLIOTH_RPC_PATH);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT, COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	k_mutex_lock(&_rpc_mutex, K_FOREVER);

	coap_reply_clear(&client->rpc.observe_reply);
	coap_reply_init(&client->rpc.observe_reply, &packet);
	client->rpc.observe_reply.reply = on_rpc;

	k_mutex_unlock(&_rpc_mutex);

	return golioth_send_coap(client, &packet);
}

void on_message(struct golioth_client *client,
		struct coap_packet *rx,
		void *user_arg)
{
	k_mutex_lock(&_rpc_mutex, K_FOREVER);
	coap_response_received(rx, NULL, &client->rpc.observe_reply, 1);
	k_mutex_unlock(&_rpc_mutex);
}

int golioth_rpc_register(struct golioth_client *client,
			 const char *method_name,
			 golioth_rpc_cb_fn callback,
			 void *callback_arg)
{
	int status = 0;

	if (client->rpc.num_methods >= CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS) {
		LOG_ERR("Unable to register, can't register more than %d methods",
			CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS);
		return -1;
	}

	k_mutex_lock(&_rpc_mutex, K_FOREVER);

	struct golioth_rpc_method *method = &client->rpc.methods[client->rpc.num_methods];

	method->name = method_name;
	method->callback = callback;
	method->callback_arg = callback_arg;

	client->rpc.num_methods++;
	if (client->rpc.num_methods == 1) {
		golioth_register_message_callback(client, on_message, NULL);
		status = golioth_rpc_observe(client);
	}

	k_mutex_unlock(&_rpc_mutex);

	return status;
}
