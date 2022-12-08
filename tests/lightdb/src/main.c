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

struct async_data {
	struct k_sem sem;
	int err;
	const uint8_t *expected_data;
	size_t expected_len;
};

static int lightdb_get_handler(struct golioth_req_rsp *rsp)
{
	struct async_data *async = rsp->user_data;

	if (rsp->err) {
		LOG_ERR("Failed to receive value: %d", rsp->err);
		k_sem_give(&async->sem);
		return rsp->err;
	}

	if (rsp->off + rsp->len > async->expected_len) {
		LOG_ERR("Response out of expected length (off=%zu len=%zu expected_len=%zu)",
			rsp->off, rsp->len, async->expected_len);
	}

	if (memcmp(&async->expected_data[rsp->off], rsp->data, rsp->len) != 0) {
		LOG_ERR("Memory not equal at off=%zu len=%zu", rsp->off, rsp->len);
		async->err = -EBADMSG;
		return -EBADMSG;
	}

	/* Mark received area */
	memset(&buffer[rsp->off], 0xff, rsp->len);

	if (rsp->get_next) {
		rsp->get_next(rsp->get_next_data, 0);
		return 0;
	}

	k_sem_give(&async->sem);

	return 0;
}

static void lightdb_get_cb(const char *path,
			   enum golioth_content_format content_format,
			   const uint8_t *expected_data, size_t expected_len)
{
	struct async_data async = {
		.err = 0,
		.expected_data = expected_data,
		.expected_len = expected_len,
	};
	int err;

	k_sem_init(&async.sem, 0, 1);

	/* Prepare 'buffer' to store information about "byte received" */
	memset(buffer, 0x0, sizeof(buffer));

	err = golioth_lightdb_get_cb(client, path, content_format,
				     lightdb_get_handler, &async);
	zassert_false(err, "Failed to request %s (%s)", path, err);

	k_sem_take(&async.sem, K_FOREVER);

	zassert_false(async.err, "Error during aync get of %s", path);

	/*
	 * So far in callback we just checked if received bytes match expected bytes. Now check if
	 * all bytes were received.
	 */
	for (size_t i = 0; i < expected_len; i++) {
		zassert_equal(buffer[i], 0xff,
			      "Byte at offset %zu was not received by callback", i);
	}
}

ZTEST(lightdb, test_lightdb_get_cb_small_cbor)
{
	lightdb_get_cb("test/get/small", GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       get_small_cbor, sizeof(get_small_cbor));
}

ZTEST(lightdb, test_lightdb_get_cb_small_json)
{
	lightdb_get_cb("test/get/small", GOLIOTH_CONTENT_FORMAT_APP_JSON,
		       get_small_json, sizeof(get_small_json));
}

ZTEST(lightdb, test_lightdb_get_cb_big_1024_cbor)
{
	lightdb_get_cb("test/get/big-1024", GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       get_big_1024_cbor, sizeof(get_big_1024_cbor));
}

ZTEST(lightdb, test_lightdb_get_cb_big_1024_json)
{
	lightdb_get_cb("test/get/big-1024", GOLIOTH_CONTENT_FORMAT_APP_JSON,
		       get_big_1024_json, sizeof(get_big_1024_json));
}

ZTEST(lightdb, test_lightdb_get_cb_big_2048_cbor)
{
	lightdb_get_cb("test/get/big-2048", GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       get_big_2048_cbor, sizeof(get_big_2048_cbor));
}

ZTEST(lightdb, test_lightdb_get_cb_big_2048_json)
{
	lightdb_get_cb("test/get/big-2048", GOLIOTH_CONTENT_FORMAT_APP_JSON,
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
