/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb_stream, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>
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

static int temperature_push_handler(struct golioth_req_rsp *rsp)
{
	if (rsp->err) {
		LOG_WRN("Failed to push temperature: %d", rsp->err);
		return rsp->err;
	}

	LOG_DBG("Temperature successfully pushed");

	return 0;
}

static void temperature_push_async(const struct sensor_value *temp)
{
	char sbuf[sizeof("-4294967295.123456")];
	int err;

	snprintk(sbuf, sizeof(sbuf), "%d.%06d", temp->val1, abs(temp->val2));

	err = golioth_stream_push_cb(client, "temp",
				     GOLIOTH_CONTENT_FORMAT_APP_JSON,
				     sbuf, strlen(sbuf),
				     temperature_push_handler, NULL);
	if (err) {
		LOG_WRN("Failed to push temperature: %d", err);
		return;
	}
}

static void temperature_push_sync(const struct sensor_value *temp)
{
	char sbuf[sizeof("-4294967295.123456")];
	int err;

	snprintk(sbuf, sizeof(sbuf), "%d.%06d", temp->val1, abs(temp->val2));

	err = golioth_stream_push(client, "temp",
				  GOLIOTH_CONTENT_FORMAT_APP_JSON,
				  sbuf, strlen(sbuf));
	if (err) {
		LOG_WRN("Failed to push temperature: %d", err);
		return;
	}

	LOG_DBG("Temperature successfully pushed");
}

int main(void)
{
	struct sensor_value temp;
	int err;

	LOG_DBG("Start LightDB Stream sample");

	net_connect();

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		/* Synchronous */
		err = get_temperature(&temp);
		if (err) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		LOG_DBG("Sending temperature %d.%06d", temp.val1, abs(temp.val2));

		temperature_push_sync(&temp);

		k_sleep(K_SECONDS(5));

		/* Callback-based */
		err = get_temperature(&temp);
		if (err) {
			k_sleep(K_SECONDS(1));
			continue;
		}

		LOG_DBG("Sending temperature %d.%06d", temp.val1, abs(temp.val2));

		temperature_push_async(&temp);

		k_sleep(K_SECONDS(5));
	}

	return 0;
}
