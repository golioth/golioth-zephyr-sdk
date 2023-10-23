/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_shell, LOG_LEVEL_DBG);

#include <net/golioth.h>
#include <samples/common/wifi.h>

#include "common.h"

static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t type;

	type = coap_header_get_type(rx);
	payload = coap_packet_get_payload(rx, &payload_len);

	if (!IS_ENABLED(CONFIG_LOG_BACKEND_GOLIOTH) && payload) {
		LOG_HEXDUMP_DBG(payload, payload_len, "Payload");
	}
}

static uint8_t golioth_shell_rx_buffer[2048];

void main(void)
{
	golioth_init(client);

	client->rx_buffer = golioth_shell_rx_buffer;
	client->rx_buffer_len = sizeof(golioth_shell_rx_buffer);

	client->on_message = golioth_on_message;

	LOG_DBG("Start Golioth shell sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	LOG_INF("Ready");
}
