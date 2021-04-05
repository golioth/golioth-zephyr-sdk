/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2021 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <errno.h>
#include <logging/golioth.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/golioth.h>
#include <net/tls_credentials.h>
#include <posix/sys/eventfd.h>

#include <drivers/gpio.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>

#define RX_TIMEOUT		K_SECONDS(30)

#define MAX_COAP_MSG_LEN	256

#define TLS_PSK_ID		CONFIG_GOLIOTH_SERVER_DTLS_PSK_ID
#define TLS_PSK			CONFIG_GOLIOTH_SERVER_DTLS_PSK

#define PSK_TAG			1

/* Golioth instance */
static struct golioth_client g_client;
static struct golioth_client *client = &g_client;

static uint8_t rx_buffer[MAX_COAP_MSG_LEN];

static struct sockaddr addr;

#define POLLFD_EVENT_RECONNECT	0
#define POLLFD_SOCKET		1

static struct zsock_pollfd fds[2];
static struct coap_reply coap_replies[2];

#define LED_GPIO_SPEC(i, _)						\
	COND_CODE_1(DT_NODE_HAS_STATUS(DT_ALIAS(led##i), okay),		\
		    (GPIO_DT_SPEC_GET(DT_ALIAS(led##i), gpios),),	\
		    ())

static struct gpio_dt_spec led[] = {
	UTIL_LISTIFY(10, LED_GPIO_SPEC)
};

static K_SEM_DEFINE(golioth_client_ready, 0, 1);

static void client_request_reconnect(void)
{
	eventfd_write(fds[POLLFD_EVENT_RECONNECT].fd, 1);
}

static void client_rx_timeout_work(struct k_work *work)
{
	LOG_ERR("RX client timeout!");

	client_request_reconnect();
}

static K_WORK_DEFINE(rx_timeout_work, client_rx_timeout_work);

static void client_rx_timeout(struct k_timer *timer)
{
	k_work_submit(&rx_timeout_work);
}

static K_TIMER_DEFINE(rx_timeout, client_rx_timeout, NULL);

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

static int init_tls(void)
{
	int err;

	err = tls_credential_add(PSK_TAG,
				TLS_CREDENTIAL_PSK,
				TLS_PSK,
				sizeof(TLS_PSK) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register PSK: %d", err);
		return err;
	}

	err = tls_credential_add(PSK_TAG,
				TLS_CREDENTIAL_PSK_ID,
				TLS_PSK_ID,
				sizeof(TLS_PSK_ID) - 1);
	if (err < 0) {
		LOG_ERR("Failed to register PSK ID: %d", err);
		return err;
	}

	return 0;
}

static int initialize_client(void)
{
	sec_tag_t sec_tag_list[] = {
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
		PSK_TAG,
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
	};
	int err;

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		init_tls();
	}

	golioth_init(client);

	client->rx_buffer = rx_buffer;
	client->rx_buffer_len = sizeof(rx_buffer);

	client->on_message = golioth_on_message;

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		err = golioth_set_proto_coap_dtls(client, sec_tag_list,
						  ARRAY_SIZE(sec_tag_list));
	} else {
		err = golioth_set_proto_coap_udp(client, TLS_PSK_ID,
						 sizeof(TLS_PSK_ID) - 1);
	}
	if (err) {
		LOG_ERR("Failed to set protocol: %d", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_NET_IPV4)) {
		struct sockaddr_in *addr4 = (struct sockaddr_in *) &addr;

		addr4->sin_family = AF_INET;
		addr4->sin_port = htons(CONFIG_GOLIOTH_SERVER_PORT);

		zsock_inet_pton(addr4->sin_family, CONFIG_GOLIOTH_SERVER_IP_ADDR,
				&addr4->sin_addr);

		client->server = (struct sockaddr *)addr4;
	} else if (IS_ENABLED(CONFIG_NET_IPV6)) {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) &addr;

		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(CONFIG_GOLIOTH_SERVER_PORT);

		zsock_inet_pton(addr6->sin6_family, CONFIG_GOLIOTH_SERVER_IP_ADDR,
				&addr6->sin6_addr);

		client->server = (struct sockaddr *)addr6;
	}

	fds[POLLFD_EVENT_RECONNECT].fd = eventfd(0, EFD_NONBLOCK);
	fds[POLLFD_EVENT_RECONNECT].events = ZSOCK_POLLIN;

	if (IS_ENABLED(CONFIG_LOG_BACKEND_GOLIOTH)) {
		log_backend_golioth_init(client);
	}

	return 0;
}

static int golioth_leds_get(struct golioth_client *client,
			    coap_reply_t reply_cb)
{
	struct coap_reply *reply =
		coap_reply_next_unused(coap_replies, ARRAY_SIZE(coap_replies));

	if (!reply) {
		LOG_ERR("No more reply handlers");
		return -ENOMEM;
	}

	return golioth_lightdb_get(client, GOLIOTH_LIGHTDB_PATH("led"),
				   COAP_CONTENT_FORMAT_APP_CBOR, reply,
				   reply_cb);
}

static int connect_client(void)
{
	struct coap_reply *observe_reply;
	int err;
	int i;

	err = golioth_connect(client);
	if (err) {
		LOG_ERR("Failed to connect: %d", err);
		return err;
	}

	fds[POLLFD_SOCKET].fd = client->sock;
	fds[POLLFD_SOCKET].events = ZSOCK_POLLIN;

	for (i = 0; i < ARRAY_SIZE(coap_replies); i++) {
		coap_reply_clear(&coap_replies[i]);
	}

	observe_reply = coap_reply_next_unused(coap_replies,
					       ARRAY_SIZE(coap_replies));
	if (!observe_reply) {
		LOG_ERR("No more reply handlers");
		return -ENOMEM;
	}

	err = golioth_lightdb_observe(client, GOLIOTH_LIGHTDB_PATH("led"),
				      COAP_CONTENT_FORMAT_APP_CBOR,
				      observe_reply, golioth_led_handle);

	err = golioth_leds_get(client, golioth_led_handle);

	return 0;
}

static void golioth_main(void *arg1, void *arg2, void *arg3)
{
	eventfd_t eventfd_value;
	int err;

	LOG_INF("Initializing golioth client");

	err = initialize_client();
	if (err) {
		LOG_ERR("Failed to initialize client: %d", err);
		return;
	}

	LOG_INF("Golioth client initialized");

	k_sem_give(&golioth_client_ready);

	while (true) {
		if (client->sock < 0) {
			LOG_INF("Starting connect");
			err = connect_client();
			if (err) {
				LOG_WRN("Failed to connect: %d", err);
				k_sleep(RX_TIMEOUT);
				continue;
			}

			/* Flush reconnect requests */
			(void)eventfd_read(fds[POLLFD_EVENT_RECONNECT].fd,
					   &eventfd_value);

			/* Add RX timeout */
			k_timer_start(&rx_timeout, RX_TIMEOUT, K_NO_WAIT);

			LOG_INF("Client connected!");
		}

		if (zsock_poll(fds, ARRAY_SIZE(fds), -1) < 0) {
			LOG_ERR("Error in poll:%d", errno);
			/* TODO: reconnect */
			break;
		}

		if (fds[POLLFD_EVENT_RECONNECT].revents) {
			(void)eventfd_read(fds[POLLFD_EVENT_RECONNECT].fd,
					   &eventfd_value);
			LOG_INF("Reconnect request");
			golioth_disconnect(client);
			continue;
		}

		if (fds[POLLFD_SOCKET].revents) {
			/* Restart timer */
			k_timer_start(&rx_timeout, RX_TIMEOUT, K_NO_WAIT);

			err = golioth_process_rx(client);
			if (err) {
				LOG_ERR("Failed to receive: %d", err);
				golioth_disconnect(client);
			}
		}
	}
}

K_THREAD_DEFINE(golioth_main_thread, 2048, golioth_main, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

void main(void)
{
	char str_counter[sizeof("4294967295")];
	int counter = 0;
	int err;

	LOG_DBG("Start Light DB sample");

	golioth_led_initialize();

	k_sem_take(&golioth_client_ready, K_FOREVER);

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

	LOG_DBG("Quit");
}
