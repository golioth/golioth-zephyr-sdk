/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_logging, LOG_LEVEL_DBG);

#include <samples/common/net_connect.h>
#include <net/golioth/system_client.h>
#include <zephyr/net/coap.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

static void func_1(int counter)
{
	LOG_DBG("Log 1: %d", counter);
}

static void func_2(int counter)
{
	LOG_DBG("Log 2: %d", counter);
}

int main(void)
{
	int counter = 0;

	LOG_DBG("Start Logging sample");

	net_connect();

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		LOG_DBG("Debug info! %d", counter);
		func_1(counter);
		func_2(counter);
		LOG_WRN("Warn: %d", counter);
		LOG_ERR("Err: %d", counter);
		LOG_HEXDUMP_INF(&(uint32_t){sys_cpu_to_le32(counter)}, 4, "Counter hexdump");

		counter++;

		k_sleep(K_SECONDS(5));
	}

	return 0;
}
