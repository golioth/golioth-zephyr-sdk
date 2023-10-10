/*
 * Copyright (c) 2022 Golioth, Inc.
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

	LOG_DBG("Counter deleted successfully");

	return 0;
}

static void counter_delete_async(struct golioth_client *client)
{
	int err;

	err = golioth_lightdb_delete_cb(client, "counter",
					counter_handler, NULL);
	if (err) {
		LOG_WRN("failed to delete data from LightDB: %d", err);
	}
}

static void counter_delete_sync(struct golioth_client *client)
{
	int err;

	err = golioth_lightdb_delete(client, "counter");
	if (err) {
		LOG_WRN("failed to delete data from LightDB: %d", err);
	}

	LOG_DBG("Counter deleted successfully");
}

int main(void)
{
	LOG_DBG("Start LightDB delete sample");

	net_connect();

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		LOG_DBG("Before request (async)");
		counter_delete_async(client);
		LOG_DBG("After request (async)");

		k_sleep(K_SECONDS(5));

		LOG_DBG("Before request (sync)");
		counter_delete_sync(client);
		LOG_DBG("After request (sync)");

		k_sleep(K_SECONDS(5));
	}

	return 0;
}
