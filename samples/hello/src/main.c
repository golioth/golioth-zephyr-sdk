/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_hello, LOG_LEVEL_DBG);

#include <errno.h>
#include <logging/golioth.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/golioth.h>
#include <net/tls_credentials.h>
#include <posix/sys/eventfd.h>

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

static int connect_client(void)
{
	int err;

	err = golioth_connect(client);
	if (err) {
		LOG_ERR("Failed to connect: %d", err);
		return err;
	}

	fds[POLLFD_SOCKET].fd = client->sock;
	fds[POLLFD_SOCKET].events = ZSOCK_POLLIN;

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
	int r;
	int counter = 0;

	LOG_DBG("Start Hello sample");

	k_sem_take(&golioth_client_ready, K_FOREVER);

	while (true) {
		LOG_INF("Sending hello! %d", counter++);
		LOG_DBG("Debug info! %d", counter);
		func_1(counter);
		func_2(counter);
		LOG_WRN("Warn: %d", counter);
		LOG_ERR("Err: %d", counter);

		r = golioth_send_hello(client);
		if (r < 0) {
			LOG_WRN("Failed to send hello!");
		}

		k_sleep(K_SECONDS(5));
	}

	LOG_DBG("Quit");
}
