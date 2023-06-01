/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>
#include <zephyr/net/coap.h>

#include <stdlib.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

/*
 * This function is registered to be called when the data
 * stored at `/counter` changes.
 */
static int counter_handler(struct golioth_req_rsp *rsp)
{
	if (rsp->err) {
		LOG_ERR("Failed to receive counter value: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_INF(rsp->data, rsp->len, "Counter");

	return 0;
}

/*
 * In the `main` function, this function is registered to be
 * called when the device connects to the Golioth server.
 */
static void golioth_on_connect(struct golioth_client *client)
{
	int err;

	/*
	 * Observe the data stored at `/counter` in LightDB.
	 * When that data is updated, the `on_update` callback
	 * will be called.
	 * This will get the value when first called, even if
	 * the value doesn't change.
	 */
	err = golioth_lightdb_observe_cb(client, "counter",
					 GOLIOTH_CONTENT_FORMAT_APP_JSON,
					 counter_handler, NULL);

	if (err) {
		LOG_WRN("failed to observe lightdb path: %d", err);
	}
}

int main(void)
{
	LOG_DBG("Start LightDB observe sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	while (true) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
