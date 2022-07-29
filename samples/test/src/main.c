/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);
#include <ztest.h>
#include <net/golioth/system_client.h>
#include <samples/common/wifi.h>

struct test_golioth_fixture {
};

static struct golioth_client *_client = GOLIOTH_SYSTEM_CLIENT_GET();
static struct test_golioth_fixture _fixture;

K_SEM_DEFINE(_connected_sem, 0, 1);

static void *test_golioth_suite_setup(void)
{
	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	return &_fixture;
}

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&_connected_sem);
}

ZTEST_F(test_golioth, test_connect)
{
	_client->on_connect = golioth_on_connect;
	golioth_system_client_start();
	zassert_equal(0, k_sem_take(&_connected_sem, K_SECONDS(10)), "failed to connect");
}

ZTEST_SUITE(test_golioth, NULL, test_golioth_suite_setup, NULL, NULL, NULL);
