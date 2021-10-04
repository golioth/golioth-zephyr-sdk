/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_logging, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/dhcpv4.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>
#include <net/net_if.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

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

static void func_1(int counter)
{
	LOG_DBG("Log 1: %d", counter);
}

static void func_2(int counter)
{
	LOG_DBG("Log 2: %d", counter);
}

void main(void)
{
	int counter = 0;

	LOG_DBG("Start Logging sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	if (IS_ENABLED(CONFIG_NET_L2_ETHERNET) && IS_ENABLED(CONFIG_NET_DHCPV4)) {
		LOG_INF("Starting DHCPv4");
		net_dhcpv4_start(net_if_get_default());
	}

	client->on_message = golioth_on_message;
	golioth_system_client_start();

	while (true) {
		LOG_DBG("Debug info! %d", counter);
		func_1(counter);
		func_2(counter);
		LOG_WRN("Warn: %d", counter);
		LOG_ERR("Err: %d", counter);
		LOG_HEXDUMP_INF(&counter, sizeof(counter), "Counter hexdump");

		counter++;

		k_sleep(K_SECONDS(5));
	}
}
