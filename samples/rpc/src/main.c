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

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static enum golioth_rpc_status on_multiply(zcbor_state_t *request_params_array,
					   zcbor_state_t *response_detail_map,
					   void *callback_arg)
{
	double a, b;
	double value;
	bool ok;

	ok = zcbor_float_decode(request_params_array, &a) &&
	     zcbor_float_decode(request_params_array, &b);
	if (!ok) {
		LOG_ERR("Failed to decode array items");
		return GOLIOTH_RPC_INVALID_ARGUMENT;
	}

	value = a * b;

	LOG_DBG("%lf * %lf = %lf", a, b, value);

	ok = zcbor_tstr_put_lit(response_detail_map, "value") &&
	     zcbor_float64_put(response_detail_map, value);
	if (!ok) {
		LOG_ERR("Failed to encode value");
		return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
	}

	return GOLIOTH_RPC_OK;
}

static void golioth_on_connect(struct golioth_client *client)
{
	int err = golioth_rpc_observe(client);
	if (err) {
		LOG_ERR("Failed to observe RPC: %d", err);
	}
}

int main(void)
{
	LOG_DBG("Start RPC sample");

	net_connect();

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	int err = golioth_rpc_register(client, "multiply", on_multiply, NULL);

	if (err) {
		LOG_ERR("Failed to register RPC: %d", err);
	}

	while (true) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
