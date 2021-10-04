/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>

#ifdef IS_ENABLED(CONFIG_NET_L2_ETHERNET))
#include <net/net_if.h>
#endif

#include <drivers/gpio.h>
#include <stdlib.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static struct coap_reply coap_replies[1];

#define LED_GPIO_SPEC(i, _)						\
	COND_CODE_1(DT_NODE_HAS_STATUS(DT_ALIAS(led##i), okay),		\
		    (GPIO_DT_SPEC_GET(DT_ALIAS(led##i), gpios),),	\
		    ())

static struct gpio_dt_spec led[] = {
	UTIL_LISTIFY(10, LED_GPIO_SPEC)
};

static void golioth_led_initialize(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(led); i++) {
		gpio_pin_configure_dt(&led[i], GPIO_OUTPUT_INACTIVE);
	}
}

static void golioth_led_set(unsigned int id, bool value)
{
	if (id >= ARRAY_SIZE(led)) {
		LOG_WRN("There is no LED %u (total %zu)", id,
			(size_t) ARRAY_SIZE(led));
		return;
	}

	gpio_pin_set(led[id].port, led[id].pin, value);
}

static void golioth_led_set_by_name(const char *name, bool value)
{
	char *endptr;
	unsigned long id;

	id = strtoul(name, &endptr, 0);
	if (endptr == name || *endptr != '\0') {
		LOG_WRN("LED name '%s' is not valid", name);
		return;
	}

	golioth_led_set(id, value);
}

static int golioth_led_handle(const struct coap_packet *response,
			      struct coap_reply *reply,
			      const struct sockaddr *from)
{
	const uint8_t *payload;
	uint16_t payload_len;
	struct cbor_buf_reader reader;
	CborParser parser;
	CborValue value;
	CborError err;

	payload = coap_packet_get_payload(response, &payload_len);

	cbor_buf_reader_init(&reader, payload, payload_len);
	err = cbor_parser_init(&reader.r, 0, &parser, &value);

	if (err != CborNoError) {
		LOG_ERR("Failed to init CBOR parser: %d", err);
		return -EINVAL;
	}

	if (cbor_value_is_boolean(&value)) {
		bool v;

		cbor_value_get_boolean(&value, &v);

		LOG_INF("LED value: %d", v);

		golioth_led_set(0, v);
	} else if (cbor_value_is_map(&value)) {
		CborValue map;
		char name[5];
		size_t name_len;
		bool v;

		err = cbor_value_enter_container(&value, &map);
		if (err != CborNoError) {
			LOG_WRN("Failed to enter map: %d", err);
			return -EINVAL;
		}

		while (!cbor_value_at_end(&map)) {
			/* key */
			if (!cbor_value_is_text_string(&map)) {
				LOG_WRN("Map key is not string: %d",
					cbor_value_get_type(&map));
				break;
			}

			name_len = sizeof(name) - 1;
			err = cbor_value_copy_text_string(&map,
							  name, &name_len,
							  &map);
			if (err != CborNoError) {
				LOG_WRN("Failed to read map key: %d", err);
				break;
			}

			name[name_len] = '\0';

			/* value */
			if (!cbor_value_is_boolean(&map)) {
				LOG_WRN("Map key is not boolean");
				break;
			}

			err = cbor_value_get_boolean(&map, &v);
			if (err != CborNoError) {
				LOG_WRN("Failed to read map key: %d", err);
				break;
			}

			err = cbor_value_advance_fixed(&map);
			if (err != CborNoError) {
				LOG_WRN("Failed to advance: %d", err);
				break;
			}

			LOG_INF("LED %s -> %d", log_strdup(name), (int) v);

			golioth_led_set_by_name(name, v);
		}

		err = cbor_value_leave_container(&value, &map);
		if (err != CborNoError) {
			LOG_WRN("Failed to enter map: %d", err);
		}
	}

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	struct coap_reply *observe_reply;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(coap_replies); i++) {
		coap_reply_clear(&coap_replies[i]);
	}

	observe_reply = coap_reply_next_unused(coap_replies,
					       ARRAY_SIZE(coap_replies));
	if (!observe_reply) {
		LOG_ERR("No more reply handlers");
		return;
	}

	err = golioth_lightdb_observe(client, GOLIOTH_LIGHTDB_PATH("led"),
				      COAP_CONTENT_FORMAT_APP_CBOR,
				      observe_reply, golioth_led_handle);
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

	(void)coap_response_received(rx, NULL, coap_replies,
				     ARRAY_SIZE(coap_replies));
}

void main(void)
{
	char str_counter[sizeof("4294967295")];
	int counter = 0;
	int err;

	LOG_DBG("Start Light DB LED sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	if (IS_ENABLED(CONFIG_NET_L2_ETHERNET))
	{
		LOG_INF("Connecting to Ethernet");
		struct net_if *iface;
		iface = net_if_get_default();
		net_dhcpv4_start(iface);
	}

	golioth_led_initialize();

	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;
	golioth_system_client_start();

	while (true) {
		snprintk(str_counter, sizeof(str_counter) - 1, "%d", counter);

		err = golioth_lightdb_set(client,
					  GOLIOTH_LIGHTDB_PATH("counter"),
					  COAP_CONTENT_FORMAT_TEXT_PLAIN,
					  str_counter, strlen(str_counter));
		if (err) {
			LOG_WRN("Failed to update counter: %d", err);
		}

		counter++;

		k_sleep(K_SECONDS(5));
	}
}
