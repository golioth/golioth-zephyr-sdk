/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_hello, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <net/golioth/settings.h>
#include <samples/common/net_connect.h>
#include <zephyr/init.h>
#include <zephyr/net/coap.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static int32_t _loop_delay_s = 5;

enum golioth_settings_status on_setting(
		const char *key,
		const struct golioth_settings_value *value)
{
	LOG_DBG("Received setting: key = %s, type = %d", key, value->type);
	if (strcmp(key, "LOOP_DELAY_S") == 0) {
		/* This setting is expected to be numeric, return an error if it's not */
		if (value->type != GOLIOTH_SETTINGS_VALUE_TYPE_INT64) {
			return GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID;
		}

		/* This setting must be in range [1, 100], return an error if it's not */
		if (value->i64 < 1 || value->i64 > 100) {
			return GOLIOTH_SETTINGS_VALUE_OUTSIDE_RANGE;
		}

		/* Setting has passed all checks, so apply it to the loop delay */
		_loop_delay_s = (int32_t)value->i64;
		LOG_INF("Set loop delay to %d seconds", _loop_delay_s);

		return GOLIOTH_SETTINGS_SUCCESS;
	}

	/* If the setting is not recognized, we should return an error */
	return GOLIOTH_SETTINGS_KEY_NOT_RECOGNIZED;
}

static void golioth_on_connect(struct golioth_client *client)
{
	if (IS_ENABLED(CONFIG_GOLIOTH_SETTINGS)) {
		golioth_settings_observe(client);
	}
}

void main(void)
{
	int counter = 0;
	int err;

	LOG_DBG("Start Hello sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	if (IS_ENABLED(CONFIG_GOLIOTH_SETTINGS)) {
		err = golioth_settings_register_callback(client, on_setting);
		if (err) {
			LOG_ERR("Failed to register settings callback: %d", err);
		}
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	while (true) {
		LOG_INF("Sending hello! %d", counter);

		err = golioth_send_hello(client);
		if (err) {
			LOG_WRN("Failed to send hello!");
		}
		++counter;
		k_sleep(K_SECONDS(_loop_delay_s));
	}
}
