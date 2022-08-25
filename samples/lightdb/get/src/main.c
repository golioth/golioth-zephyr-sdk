/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/wifi.h>
#include <zephyr/net/coap.h>

#include <stdlib.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static struct coap_reply coap_replies[1];

K_MUTEX_DEFINE(coap_reply_mutex);

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

/*
 * This function is registered to be called when the reply
 * is received from the Golioth server.
 */
static int reply_callback(const struct coap_packet *response,
			  struct coap_reply *reply,
			  const struct sockaddr *from)
{
	char str[64];
	uint16_t payload_len;
	const uint8_t *payload;

	payload = coap_packet_get_payload(response, &payload_len);
	if (!payload) {
		LOG_WRN("packet did not contain data");
		return -ENOMSG;
	}

	if (payload_len + 1 > ARRAY_SIZE(str)) {
		payload_len = ARRAY_SIZE(str) - 1;
	}

	memcpy(str, payload, payload_len);
	str[payload_len] = '\0';

	LOG_DBG("payload: %s", str);

	return 0;
}

/*
 * In the `main` function, this function is registered to be
 * called when the device receives a packet from the Golioth server.
 */
static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	/* We're returning the reply to the pool, so we need to lock the mutex. */
	k_mutex_lock(&coap_reply_mutex, K_FOREVER);

	/*
	 * In order for the observe callback to be called, we need to call this
	 * function.
	 */
	coap_response_received(rx, NULL, coap_replies,
			       ARRAY_SIZE(coap_replies));

	k_mutex_unlock(&coap_reply_mutex);
}

void main(void)
{
	int err;
	struct coap_reply *reply;

	LOG_DBG("Start Light DB get sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	coap_replies_clear(coap_replies, ARRAY_SIZE(coap_replies));

	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	while (true) {
		k_mutex_lock(&coap_reply_mutex, K_FOREVER);
		reply = coap_reply_next_unused(coap_replies,
					ARRAY_SIZE(coap_replies));
		k_mutex_unlock(&coap_reply_mutex);

		if (reply) {
			/*
			 * When the reply is received from the Golioth server,
			 * the `reply_callback` function will be called.
			 *
			 * In this case, we're requesting that the data be
			 * transmitted as JSON, so we don't have to decode it to
			 * display it.
			 */
			err = golioth_lightdb_get(client,
						  GOLIOTH_LIGHTDB_PATH("counter"),
						  GOLIOTH_CONTENT_FORMAT_APP_JSON,
						  reply, reply_callback);
			if (err) {
				LOG_WRN("failed to get data from LightDB: %d", err);
			}
		}

		k_sleep(K_SECONDS(5));

		if (reply) {
			k_mutex_lock(&coap_reply_mutex, K_FOREVER);

			/*
			 * Clear the reply so it can be used again in the
			 * future. This is effectively enforcing a 5 second
			 * timeout on requests.
			 */
			coap_reply_clear(reply);

			k_mutex_unlock(&coap_reply_mutex);
		}
	}
}
