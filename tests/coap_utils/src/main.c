/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coap_utils_test);

#include <zephyr/ztest.h>

#include <zephyr/net/coap.h>

#include "coap_utils.h"
#include "pathv.h"

static uint8_t buffer[512];

static size_t coap_packet_appended_pathv_length(const uint8_t **pathv)
{
	struct coap_packet packet;
	size_t before;
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       0, NULL,
			       COAP_METHOD_GET, coap_next_id());
	zassert_equal(err, 0, "Unable to initialize packet");

	before = packet.offset;

	err = coap_packet_append_uri_path_from_pathv(&packet, pathv);
	zassert_equal(err, 0, "Unable to append pathv");

	return packet.offset - before;
}

ZTEST(coap_utils, test_pathv_estimate_alloc_len)
{
	const uint8_t **path;
	size_t appended_len, estimated_len;

	path = PATHV("1234567890123/1234567890123");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV("1234567890123/1234567890123", "1234567890123");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV("1234567890123/1234567890123", "1234567890123/1234567890123");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV("1234567890123/1234567890123",
	      "1234567890123/1234567890123/1234567890123/1234567890123/1234567890123/1234567890123/1234567890123");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV("1234567890123-1234567890123",
	      "1234567890123-1234567890123-1234567890123/1234567890123-1234567890123-1234567890123-1234567890123");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV(".d", "counter");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");

	path = PATHV("1", "2", "3", "4", "5");
	appended_len = coap_packet_appended_pathv_length(path);
	estimated_len = coap_pathv_estimate_alloc_len(path);
	LOG_INF("appended_len %zu estimated_len %zu", appended_len, estimated_len);
	zassert_true(appended_len <= estimated_len,
		     "Estimated length is lower than appended length");
}

ZTEST_SUITE(coap_utils, NULL, NULL, NULL, NULL, NULL);
