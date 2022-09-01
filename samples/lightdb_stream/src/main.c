/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb_stream, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/wifi.h>
#include <zephyr/net/coap.h>

#include <zephyr/drivers/sensor.h>
#include <stdlib.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

#if DT_NODE_HAS_STATUS(DT_ALIAS(temp0), okay)

static int get_temperature(struct sensor_value *val)
{
	const struct device *dev = DEVICE_DT_GET(DT_ALIAS(temp0));
	static const enum sensor_channel temp_channels[] = {
		SENSOR_CHAN_AMBIENT_TEMP,
		SENSOR_CHAN_DIE_TEMP,
	};
	int i;
	int err;

	err = sensor_sample_fetch(dev);
	if (err) {
		LOG_ERR("Failed to fetch temperature sensor: %d", err);
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(temp_channels); i++) {
		err = sensor_channel_get(dev, temp_channels[i], val);
		if (err == -ENOTSUP) {
			/* try next channel */
			continue;
		} else if (err) {
			LOG_ERR("Failed to get temperature: %d", err);
			return err;
		}
	}

	return err;
}

#else

static int get_temperature(struct sensor_value *val)
{
	static int counter = 0;

	/* generate a temperature from 20 deg to 30 deg, with 0.5 deg step */

	val->val1 = 20 + counter / 2;
	val->val2 = counter % 2 == 1 ? 500000 : 0;

	counter = (counter + 1) % 20;

	return 0;
}

#endif

void main(void)
{
	char str_temperature[32];
	struct sensor_value temp;
	int err;

	LOG_DBG("Start LightDB stream sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		err = get_temperature(&temp);
		if (err) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		snprintk(str_temperature, sizeof(str_temperature),
			 "%d.%06d", temp.val1, abs(temp.val2));
		str_temperature[sizeof(str_temperature) - 1] = '\0';

		LOG_DBG("Sending temperature %s", str_temperature);

		err = golioth_lightdb_set(client,
					  GOLIOTH_LIGHTDB_STREAM_PATH("temp"),
					  GOLIOTH_CONTENT_FORMAT_APP_JSON,
					  str_temperature,
					  strlen(str_temperature));
		if (err) {
			LOG_WRN("Failed to send temperature: %d", err);
		}

		k_sleep(K_SECONDS(5));
	}
}
