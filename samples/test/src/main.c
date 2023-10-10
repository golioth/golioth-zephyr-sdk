/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

#include <zephyr/ztest.h>

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>

struct test_golioth_fixture {
};

static struct golioth_client *_client = GOLIOTH_SYSTEM_CLIENT_GET();
static struct test_golioth_fixture _fixture;

K_SEM_DEFINE(_connected_sem, 0, 1);

static void *test_golioth_suite_setup(void)
{
	net_connect();

	return &_fixture;
}

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&_connected_sem);
}

ZTEST_F(test_golioth, test_connect)
{
	int err;

	_client->on_connect = golioth_on_connect;
	golioth_system_client_start();
	err = k_sem_take(&_connected_sem, K_SECONDS(CONFIG_GOLIOTH_CONNECTION_TEST_TIMEOUT));
	zassert_false(err, "failed to connect");
}

ZTEST_SUITE(test_golioth, NULL, test_golioth_suite_setup, NULL, NULL, NULL);
