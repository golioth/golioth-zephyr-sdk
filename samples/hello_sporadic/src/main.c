/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_hello, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

K_SEM_DEFINE(sys_client_conn_resolved, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&sys_client_conn_resolved);
}

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

void main(void)
{
	int counter = 0;
	int err;

	LOG_DBG("Start Hello Sporadic sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	client->on_message = golioth_on_message;
	client->on_connect = golioth_on_connect;

	while (true) {

		golioth_system_client_start();
		k_sem_take(&sys_client_conn_resolved, K_FOREVER);

		LOG_INF("Sending hello! %d", counter++);

		err = golioth_send_hello(client);
		if (err) {
			LOG_WRN("Failed to send hello!");
		}
		golioth_system_client_stop();

		k_sleep(K_SECONDS(60));
	}
}
