/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/rpc.h>
#include <net/golioth/system_client.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <stdio.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"

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

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#include "golioth_utils.h"

#define GOLIOTH_RPC_PATH ".rpc"
#define GOLIOTH_RPC_STATUS_PATH ".rpc/status"
#define GOLIOTH_RPC_MAX_RESPONSE_LEN 256

static int send_response(struct golioth_client *client,
			 uint8_t *coap_payload, size_t coap_payload_len)
{
	return golioth_coap_req_cb(client, COAP_METHOD_POST, PATHV(GOLIOTH_RPC_STATUS_PATH),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   coap_payload, coap_payload_len,
				   golioth_req_rsp_default_handler, "RPC response ACK",
				   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

static int on_rpc(struct golioth_req_rsp *rsp)
{
	struct golioth_client *client = rsp->user_data;

	if (rsp->err) {
		LOG_ERR("Error on RPC observation: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, "Payload");

	if (rsp->len == 3 && rsp->data[1] == 'O' && rsp->data[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	UsefulBufC payload_buf = { rsp->data, rsp->len };

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

	k_mutex_lock(&client->rpc.mutex, K_FOREVER);

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

	k_mutex_unlock(&client->rpc.mutex);

	QCBOREncode_AddUInt64ToMap(&encode_ctx, "statusCode", status_code);

	QCBOREncode_CloseMap(&encode_ctx); /* root response map */
	QCBORDecode_ExitMap(&decode_ctx); /* root request map */

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
	return golioth_coap_req_cb(client, COAP_METHOD_GET, PATHV(GOLIOTH_RPC_PATH),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   NULL, 0,
				   on_rpc, client,
				   GOLIOTH_COAP_REQ_OBSERVE);
}

int golioth_rpc_init(struct golioth_client *client)
{
	return k_mutex_init(&client->rpc.mutex);
}

int golioth_rpc_register(struct golioth_client *client,
			 const char *method_name,
			 golioth_rpc_cb_fn callback,
			 void *callback_arg)
{
	int status = 0;

	k_mutex_lock(&client->rpc.mutex, K_FOREVER);

	if (client->rpc.num_methods >= CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS) {
		LOG_ERR("Unable to register, can't register more than %d methods",
			CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS);
		status = -ENOBUFS;
		goto cleanup;
	}

	struct golioth_rpc_method *method = &client->rpc.methods[client->rpc.num_methods];

	method->name = method_name;
	method->callback = callback;
	method->callback_arg = callback_arg;

	client->rpc.num_methods++;
	if (client->rpc.num_methods == 1) {
		status = golioth_rpc_observe(client);
	}

cleanup:
	k_mutex_unlock(&client->rpc.mutex);
	return status;
}
