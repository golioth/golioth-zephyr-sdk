/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/wifi.h>
#include <zephyr/net/coap.h>

#include <stdlib.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

/*
 * This function stores `counter` in lightdb at `/counter`.
 */
static void counter_set(int counter)
{
	char sbuf[sizeof("4294967295")];
	int err;

	snprintk(sbuf, sizeof(sbuf) - 1, "%d", counter);

	err = golioth_lightdb_set(client,
				  GOLIOTH_LIGHTDB_PATH("counter"),
				  COAP_CONTENT_FORMAT_TEXT_PLAIN,
				  sbuf, strlen(sbuf));
	if (err) {
		LOG_WRN("Failed to update counter: %d", err);
	}
}

void main(void)
{
	int counter = 0;

	LOG_DBG("Start Light DB set sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	golioth_system_client_start();

	while (true) {
		LOG_DBG("Setting counter to %d", counter);
		counter_set(counter);

		counter++;

		k_sleep(K_SECONDS(5));
	}
}
