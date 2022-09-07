/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>

#include <stdlib.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

static int counter_handler(struct golioth_req_rsp *rsp)
{
	if (rsp->err) {
		LOG_ERR("Failed to receive counter value: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_INF(rsp->data, rsp->len, "Counter (async)");

	return 0;
}

static void counter_get_async(struct golioth_client *client)
{
	int err;

	err = golioth_lightdb_get_cb(client, "counter",
				     GOLIOTH_CONTENT_FORMAT_APP_JSON,
				     counter_handler, NULL);
	if (err) {
		LOG_WRN("failed to get data from LightDB: %d", err);
	}
}

static void counter_get_sync(struct golioth_client *client)
{
	uint8_t value[128];
	size_t len = sizeof(value);
	int err;

	err = golioth_lightdb_get(client, "counter",
				  GOLIOTH_CONTENT_FORMAT_APP_JSON,
				  value, &len);
	if (err) {
		LOG_WRN("failed to get data from LightDB: %d", err);
	}

	LOG_HEXDUMP_INF(value, len, "Counter (sync)");
}

void main(void)
{
	LOG_DBG("Start LightDB get sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		LOG_INF("Before request (async)");
		counter_get_async(client);
		LOG_INF("After request (async)");

		k_sleep(K_SECONDS(5));

		LOG_INF("Before request (sync)");
		counter_get_sync(client);
		LOG_INF("After request (sync)");

		k_sleep(K_SECONDS(5));

#if 0
		err = golioth_lightdb_get_auto(client, "counter", &counter);
		if (err && err != -ERANGE) {
			LOG_WRN("failed to get counter from LightDB: %d", err);
		} else {
			LOG_INF("Counter value: %d%s", (int) counter,
				err == -ERANGE ? " (clamped)" : "");
		}
#endif
	}
}
