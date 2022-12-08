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

const uint8_t cbor_null[] = { 0xf6 };

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

static void lightdb_get(const char *path,
			enum golioth_content_format content_format,
			int expected_err,
			const uint8_t *expected_data, size_t expected_len)
{
	size_t len;
	int err;

	len = sizeof(buffer);

	err = golioth_lightdb_get(client, path, content_format,
				  buffer, &len);

	zassert_equal(err, expected_err,
		      "Getting %s resulted in %d error code, but expected %d",
		      path, err, expected_err);

	if (err) {
		/* Response is not valid */
		return;
	}

	LOG_HEXDUMP_DBG(buffer, len, "received");
	LOG_HEXDUMP_DBG(expected_data, expected_len, "expected");

	zassert_mem_equal(buffer, expected_data, expected_len,
			  "Contents are not as expected");
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
		async->err = rsp->err;
		k_sem_give(&async->sem);
		return rsp->err;
	}

	LOG_INF("Received value");

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
			   void (*after_req_fn)(void),
			   int expected_request_err, int expected_async_err,
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

	zassert_equal(err, expected_request_err,
		      "Getting %s resulted in %d error code, but expected %d",
		      path, err, expected_request_err);

	if (err) {
		/* Response not valid */
		return;
	}

	if (after_req_fn) {
		after_req_fn();
	}

	k_sem_take(&async.sem, K_FOREVER);

	zassert_equal(async.err, expected_async_err,
		      "Getting %s resulted in %d error code, but expected %d",
		      path, async.err, expected_async_err);

	if (async.err) {
		/* Response not valid */
		return;
	}

	/*
	 * So far in callback we just checked if received bytes match expected bytes. Now check if
	 * all bytes were received.
	 */
	for (size_t i = 0; i < expected_len; i++) {
		zassert_equal(buffer[i], 0xff,
			      "Byte at offset %zu was not received by callback", i);
	}
}

ZTEST(suite_01_disconnected, test_lightdb_get_cbor_null)
{
	lightdb_get("test/non/existing",
		    GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		    -ENETDOWN,
		    cbor_null, sizeof(cbor_null));
}

ZTEST(suite_01_disconnected, test_lightdb_get_cb_cbor_null)
{
	lightdb_get_cb("test/non/existing",
		       GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       NULL,
		       -ENETDOWN, 0,
		       cbor_null, sizeof(cbor_null));
}

ZTEST_SUITE(suite_01_disconnected, NULL, NULL, NULL, NULL, NULL);

ZTEST(suite_02_connected, test_lightdb_get_cbor_null)
{
	lightdb_get("test/non/existing",
		    GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		    0, cbor_null, sizeof(cbor_null));
}

ZTEST(suite_02_connected, test_lightdb_get_cb_cbor_null)
{
	lightdb_get_cb("test/non/existing",
		       GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       NULL,
		       0, 0,
		       cbor_null, sizeof(cbor_null));
}

ZTEST_SUITE(suite_02_connected, NULL, network_setup, NULL, NULL, NULL);

ZTEST(suite_03_disconnected_after_request, test_lightdb_get_cb_cbor_null)
{
	lightdb_get_cb("test/non/existing",
		       GOLIOTH_CONTENT_FORMAT_APP_CBOR,
		       golioth_system_client_stop,
		       0, -ESHUTDOWN,
		       cbor_null, sizeof(cbor_null));
}

ZTEST_SUITE(suite_03_disconnected_after_request, NULL, NULL, NULL, NULL, NULL);
