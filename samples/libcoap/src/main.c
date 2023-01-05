/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(libcoap_hello, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>

#include <samples/common/net_connect.h>

void main(void)
{
	int counter = 0;

	LOG_DBG("Start libcoap hello sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	while (true) {
		LOG_INF("Sending hello! %d", counter);

		LOG_WRN("TODO: golioth_send_hello() implement");

		++counter;
		k_sleep(K_SECONDS(5));
	}
}
