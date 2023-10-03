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

static int counter_set_handler(struct golioth_req_rsp *rsp)
{
	if (rsp->err) {
		LOG_WRN("Failed to set counter: %d", rsp->err);
		return rsp->err;
	}

	LOG_DBG("Counter successfully set");

	return 0;
}

static void counter_set_async(int counter)
{
	char sbuf[sizeof("4294967295")];
	int err;

	snprintk(sbuf, sizeof(sbuf), "%d", counter);
	sbuf[sizeof(sbuf) - 1] = '\0';

	err = golioth_lightdb_set_cb(client, "counter",
				     GOLIOTH_CONTENT_FORMAT_APP_JSON,
				     sbuf, strlen(sbuf),
				     counter_set_handler, NULL);
	if (err) {
		LOG_WRN("Failed to set counter: %d", err);
		return;
	}
}

static void counter_set_sync(int counter)
{
	char sbuf[sizeof("4294967295")];
	int err;

	snprintk(sbuf, sizeof(sbuf), "%d", counter);
	sbuf[sizeof(sbuf) - 1] = '\0';

	err = golioth_lightdb_set(client, "counter",
				  GOLIOTH_CONTENT_FORMAT_APP_JSON,
				  sbuf, strlen(sbuf));
	if (err) {
		LOG_WRN("Failed to set counter: %d", err);
		return;
	}

	LOG_DBG("Counter successfully set");
}

int main(void)
{
	int counter = 0;

	LOG_DBG("Start LightDB set sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		LOG_DBG("Setting counter to %d", counter);

		LOG_DBG("Before request (async)");
		counter_set_async(counter);
		LOG_DBG("After request (async)");

		counter++;
		k_sleep(K_SECONDS(5));

		LOG_DBG("Setting counter to %d", counter);

		LOG_DBG("Before request (sync)");
		counter_set_sync(counter);
		LOG_DBG("After request (sync)");

		counter++;
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
