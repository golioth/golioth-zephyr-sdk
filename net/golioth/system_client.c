/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_system, CONFIG_GOLIOTH_SYSTEM_CLIENT_LOG_LEVEL);

#include <errno.h>
#include <logging/golioth.h>
#include <net/golioth/system_client.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <posix/sys/eventfd.h>

#define RX_TIMEOUT							\
	K_SECONDS(CONFIG_GOLIOTH_SYSTEM_CLIENT_RX_TIMEOUT_SEC)

#define RX_BUFFER_SIZE		CONFIG_GOLIOTH_SYSTEM_CLIENT_RX_BUF_SIZE

#define TLS_PSK_ID		CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID
#define TLS_PSK			CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK

#define PSK_TAG			1

enum pollfd_type {
	POLLFD_EVENT_RECONNECT,
	POLLFD_SOCKET,
	NUM_POLLFDS,
};

/* Golioth instance */
struct golioth_client _golioth_system_client;

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static struct sockaddr addr;

static struct zsock_pollfd fds[NUM_POLLFDS];

static sec_tag_t sec_tag_list[] = {
	PSK_TAG,
};

static inline void client_request_reconnect(void)
{
	eventfd_write(fds[POLLFD_EVENT_RECONNECT].fd, 1);
}

static void client_rx_timeout(struct k_work *work)
{
	LOG_ERR("RX client timeout!");

	client_request_reconnect();
}

static K_WORK_DELAYABLE_DEFINE(rx_timeout, client_rx_timeout);

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

static int client_initialize(struct golioth_client *client)
{
	int err;

	golioth_init(client);

	client->rx_buffer = rx_buffer;
	client->rx_buffer_len = sizeof(rx_buffer);

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
		addr4->sin_port = htons(CONFIG_GOLIOTH_SYSTEM_SERVER_PORT);

		zsock_inet_pton(addr4->sin_family,
				CONFIG_GOLIOTH_SYSTEM_SERVER_IP_ADDR,
				&addr4->sin_addr);

		client->server = (struct sockaddr *)addr4;
	} else if (IS_ENABLED(CONFIG_NET_IPV6)) {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) &addr;

		addr6->sin6_family = AF_INET6;
		addr6->sin6_port = htons(CONFIG_GOLIOTH_SYSTEM_SERVER_PORT);

		zsock_inet_pton(addr6->sin6_family,
				CONFIG_GOLIOTH_SYSTEM_SERVER_IP_ADDR,
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

static int golioth_system_init(const struct device *dev)
{
	struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
	int err;

	LOG_INF("Initializing");

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		init_tls();
	}

	err = client_initialize(client);
	if (err) {
		LOG_ERR("Failed to initialize client: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(golioth_system_init, APPLICATION,
	 CONFIG_GOLIOTH_SYSTEM_CLIENT_INIT_PRIORITY);

static int client_connect(struct golioth_client *client)
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

static void golioth_system_client_main(void *arg1, void *arg2, void *arg3)
{
	struct golioth_client *client = arg1;
	eventfd_t eventfd_value;
	int err;

	while (true) {
		if (client->sock < 0) {
			LOG_INF("Starting connect");
			err = client_connect(client);
			if (err) {
				LOG_WRN("Failed to connect: %d", err);
				k_sleep(K_SECONDS(5));
				continue;
			}

			/* Add RX timeout */
			k_work_reschedule(&rx_timeout, RX_TIMEOUT);

			/* Flush reconnect requests */
			(void)eventfd_read(fds[POLLFD_EVENT_RECONNECT].fd,
					   &eventfd_value);

			LOG_INF("Client connected!");
		}

		if (zsock_poll(fds, ARRAY_SIZE(fds), -1) < 0) {
			LOG_ERR("Error in poll:%d", errno);
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
			k_work_reschedule(&rx_timeout, RX_TIMEOUT);

			err = golioth_process_rx(client);
			if (err) {
				LOG_ERR("Failed to receive: %d", err);
				golioth_disconnect(client);
			}
		}
	}
}

K_THREAD_DEFINE(golioth_system, 2048, golioth_system_client_main,
		GOLIOTH_SYSTEM_CLIENT_GET(), NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, SYS_FOREVER_MS);

void golioth_system_client_start(void)
{
	k_thread_start(golioth_system);
}
