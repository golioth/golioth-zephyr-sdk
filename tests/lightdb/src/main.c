/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lightdb_test);

#include <zephyr/ztest.h>

#include <net/golioth.h>
#include <net/golioth/system_client.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static uint8_t buffer[8192];

static const uint8_t get_small_cbor[] = {
#include "get-small-cbor.inc"
};
static const uint8_t get_small_json[] = {
#include "get-small-json.inc"
};

static const uint8_t get_big_1024_cbor[] = {
#include "get-big-1024-cbor.inc"
};
static const uint8_t get_big_1024_json[] = {
#include "get-big-1024-json.inc"
};

static const uint8_t get_big_2048_cbor[] = {
#include "get-big-2048-cbor.inc"
};
static const uint8_t get_big_2048_json[] = {
#include "get-big-2048-json.inc"
};

static void lightdb_get(const char *path,
			enum golioth_content_format content_format,
			const uint8_t *expected_data, size_t expected_len)
{
	size_t len;
	int err;

	len = sizeof(buffer);

	err = golioth_lightdb_get(client, path, content_format,
				  buffer, &len);

	zassert_false(err, "Getting %s was not successful (err %d)", path, err);

	LOG_HEXDUMP_DBG(buffer, len, "received");
	LOG_HEXDUMP_DBG(expected_data, expected_len, "expected");

	zassert_mem_equal(buffer, expected_data, expected_len,
			  "Contents are not as expected");
}

ZTEST(lightdb, test_lightdb_get_small_cbor)
{
	lightdb_get("test/get/small",
		    GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		    get_small_cbor, sizeof(get_small_cbor));
}

ZTEST(lightdb, test_lightdb_get_small_json)
{
	lightdb_get("test/get/small",
		    GOLIOTH_CONTENT_FORMAT_APP_JSON,
		    get_small_json, sizeof(get_small_json));
}

ZTEST(lightdb, test_lightdb_get_big_1024_cbor)
{
	lightdb_get("test/get/big-1024",
		    GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		    get_big_1024_cbor, sizeof(get_big_1024_cbor));
}

ZTEST(lightdb, test_lightdb_get_big_1024_json)
{
	lightdb_get("test/get/big-1024",
		    GOLIOTH_CONTENT_FORMAT_APP_JSON,
		    get_big_1024_json, sizeof(get_big_1024_json));
}

ZTEST(lightdb, test_lightdb_get_big_2048_cbor)
{
	lightdb_get("test/get/big-2048",
		    GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		    get_big_2048_cbor, sizeof(get_big_2048_cbor));
}

ZTEST(lightdb, test_lightdb_get_big_2048_json)
{
	lightdb_get("test/get/big-2048",
		    GOLIOTH_CONTENT_FORMAT_APP_JSON,
		    get_big_2048_json, sizeof(get_big_2048_json));
}

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

static void *network_setup(void)
{
	LOG_INF("network_setup");

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	return NULL;
}

ZTEST_SUITE(lightdb, NULL, network_setup, NULL, NULL, NULL);
