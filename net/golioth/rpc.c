/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/rpc.h>
#include <net/golioth/system_client.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <stdio.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"
#include "zcbor_utils.h"

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

static int send_response(struct golioth_client *client,
			 uint8_t *coap_payload, size_t coap_payload_len)
{
	return golioth_coap_req_cb(client, COAP_METHOD_POST, PATHV(GOLIOTH_RPC_STATUS_PATH),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   coap_payload, coap_payload_len,
				   golioth_req_rsp_default_handler, "RPC response ACK",
				   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

static int params_decode(zcbor_state_t *zsd, void *value)
{
	zcbor_state_t *params_zsd = value;
	bool ok;

	ok = zcbor_list_start_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not start CBOR list correctly");
		return -EBADMSG;
	}

	memcpy(params_zsd, zsd, sizeof(*params_zsd));

	while (!zcbor_list_or_map_end(zsd)) {
		ok = zcbor_any_skip(zsd, NULL);
		if (!ok) {
			LOG_WRN("Failed to skip param");
			return -EBADMSG;
		}
	}

	ok = zcbor_list_end_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not end CBOR list correctly");
		return -EBADMSG;
	}

	return 0;
}

static int rpc_find_and_call(struct golioth_client *client,
			     zcbor_state_t *zse,
			     zcbor_state_t *params_zsd,
			     struct zcbor_string *method,
			     enum golioth_rpc_status *status_code)
{
	const struct golioth_rpc_method *matching_method = NULL;
	bool ok;

	for (int i = 0; i < client->rpc.num_methods; i++) {
		const struct golioth_rpc_method *rpc_method = &client->rpc.methods[i];

		if (strlen(rpc_method->name) == method->len &&
		    strncmp(rpc_method->name, method->value, method->len) == 0) {
			matching_method = rpc_method;
			break;
		}
	}

	if (!matching_method) {
		*status_code = GOLIOTH_RPC_UNKNOWN;
		return 0;
	}

	LOG_DBG("Calling registered RPC method: %s", matching_method->name);

	/**
	 * Call callback while decode context is inside the params array
	 * and encode context is inside the detail map.
	 */
	ok = zcbor_tstr_put_lit(zse, "detail");
	if (!ok) {
		LOG_ERR("Failed to encode RPC '%s'", "detail");
		return -ENOMEM;
	}

	ok = zcbor_map_start_encode(zse, SIZE_MAX);
	if (!ok) {
		LOG_ERR("Did not start CBOR map correctly");
		return -ENOMEM;
	}

	*status_code = matching_method->callback(params_zsd,
						 zse,
						 matching_method->callback_arg);

	ok = zcbor_list_end_encode(zse, SIZE_MAX);
	if (!ok) {
		LOG_ERR("Failed to close '%s'", "detail");
		return -ENOMEM;
	}

	return 0;
}

static int on_rpc(struct golioth_req_rsp *rsp)
{
	ZCBOR_STATE_D(zsd, 2, rsp->data, rsp->len, 1);
	zcbor_state_t params_zsd;
	struct golioth_client *client = rsp->user_data;
	struct zcbor_string id, method;
	struct zcbor_map_entry map_entries[] = {
		ZCBOR_TSTR_LIT_MAP_ENTRY("id", zcbor_map_tstr_decode, &id),
		ZCBOR_TSTR_LIT_MAP_ENTRY("method", zcbor_map_tstr_decode, &method),
		ZCBOR_TSTR_LIT_MAP_ENTRY("params", params_decode, &params_zsd),
	};
	enum golioth_rpc_status status_code;
	int err;
	bool ok;

	if (rsp->err) {
		LOG_ERR("Error on RPC observation: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, "Payload");

	if (rsp->len == 3 && rsp->data[1] == 'O' && rsp->data[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	/* Decode request */
	err = zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
	if (err) {
		LOG_ERR("Failed to parse tstr map");
		return err;
	}

	/* Start encoding response */
	uint8_t response_buf[CONFIG_GOLIOTH_RPC_MAX_RESPONSE_LEN];
	ZCBOR_STATE_E(zse, 1, response_buf, sizeof(response_buf), 1);

	ok = zcbor_map_start_encode(zse, 1);
	if (!ok) {
		LOG_ERR("Failed to encode RPC response map");
		return -ENOMEM;
	}

	ok = zcbor_tstr_put_lit(zse, "id") &&
		zcbor_tstr_encode(zse, &id);
	if (!ok) {
		LOG_ERR("Failed to encode RPC '%s'", "id");
		return -ENOMEM;
	}

	k_mutex_lock(&client->rpc.mutex, K_FOREVER);
	err = rpc_find_and_call(client, zse, &params_zsd, &method, &status_code);
	k_mutex_unlock(&client->rpc.mutex);

	if (err) {
		return err;
	}

	ok = zcbor_tstr_put_lit(zse, "statusCode") &&
		zcbor_uint64_put(zse, status_code);
	if (!ok) {
		LOG_ERR("Failed to encode RPC '%s'", "statusCode");
		return -ENOMEM;
	}

	/* root response map */
	ok = zcbor_map_end_encode(zse, 1);
	if (!ok) {
		LOG_ERR("Failed to close '%s'", "root");
		return -ENOMEM;
	}

	LOG_HEXDUMP_DBG(response_buf, zse->payload - response_buf, "Response");

	/* Send CoAP response packet */
	return send_response(client, response_buf, zse->payload - response_buf);
}

int golioth_rpc_observe(struct golioth_client *client)
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

cleanup:
	k_mutex_unlock(&client->rpc.mutex);
	return status;
}
