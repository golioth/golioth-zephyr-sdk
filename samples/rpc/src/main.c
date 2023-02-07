/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_rpc, LOG_LEVEL_DBG);

#include <zephyr/net/coap.h>
#include <net/golioth/system_client.h>
#include <net/golioth/rpc.h>
#include <samples/common/net_connect.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static enum golioth_rpc_status on_multiply(QCBORDecodeContext *request_params_array,
					   QCBOREncodeContext *response_detail_map,
					   void *callback_arg)
{
	double a, b;
	double value;
	QCBORError qerr;

	QCBORDecode_GetDouble(request_params_array, &a);
	QCBORDecode_GetDouble(request_params_array, &b);
	qerr = QCBORDecode_GetError(request_params_array);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to decode array items: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return GOLIOTH_RPC_INVALID_ARGUMENT;
	}

	value = a * b;
	QCBOREncode_AddDoubleToMap(response_detail_map, "value", value);
	return GOLIOTH_RPC_OK;
}

static void golioth_on_connect(struct golioth_client *client)
{
	int err = golioth_rpc_observe(client);
	if (err) {
		LOG_ERR("Failed to observe RPC: %d", err);
	}
}

void main(void)
{
	LOG_DBG("Start RPC sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	int err = golioth_rpc_register(client, "multiply", on_multiply, NULL);

	if (err) {
		LOG_ERR("Failed to register RPC: %d", err);
	}

	while (true) {
		k_sleep(K_SECONDS(5));
	}
}
