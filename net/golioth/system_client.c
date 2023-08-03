/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_system, CONFIG_GOLIOTH_SYSTEM_CLIENT_LOG_LEVEL);

#include <errno.h>
#include <logging/golioth.h>
#include <net/golioth/system_client.h>
#include <net/golioth/rpc.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/posix/sys/eventfd.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define RX_BUFFER_SIZE		CONFIG_GOLIOTH_SYSTEM_CLIENT_RX_BUF_SIZE

static const uint8_t tls_ca_crt[] = {
#if defined(CONFIG_GOLIOTH_SYSTEM_CLIENT_CA_PATH)
#include "golioth-systemclient-ca.inc"
#endif
};

#define PING_INTERVAL		(CONFIG_GOLIOTH_SYSTEM_CLIENT_PING_INTERVAL_SEC * 1000)
#define RECV_TIMEOUT		(CONFIG_GOLIOTH_SYSTEM_CLIENT_RX_TIMEOUT_SEC * 1000)

enum pollfd_type {
	POLLFD_EVENT,
	POLLFD_SOCKET,
	NUM_POLLFDS,
};

/* Golioth instance */
struct golioth_client _golioth_system_client;

static uint8_t rx_buffer[RX_BUFFER_SIZE];

static struct zsock_pollfd fds[NUM_POLLFDS];

static sec_tag_t sec_tag_list[] = {
	CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
};

enum {
	FLAG_RECONNECT,
	FLAG_STOP_CLIENT,
};

static atomic_t flags;

static inline void client_notify_timeout(void)
{
	eventfd_write(fds[POLLFD_EVENT].fd, 1);
}

static void golioth_system_client_wakeup(struct golioth_client *client)
{
	eventfd_write(fds[POLLFD_EVENT].fd, 1);
}

static void eventfd_timeout_handle(struct k_work *work)
{
	client_notify_timeout();
}

static K_WORK_DELAYABLE_DEFINE(eventfd_timeout, eventfd_timeout_handle);

static bool contains_char(const uint8_t *str, size_t str_len, uint8_t c)
{
	for (const uint8_t *p = str; p < &str[str_len]; p++) {
		if (*p == c) {
			return true;
		}
	}

	return false;
}

static bool golioth_psk_id_is_valid(const uint8_t *psk_id, size_t psk_id_len)
{
	return contains_char(psk_id, psk_id_len, '@');
}

static bool golioth_psk_is_valid(const uint8_t *psk, size_t psk_len)
{
	return (psk_len > 0);
}

static int golioth_check_psk_credentials(void)
{
	int err = 0;
	uint8_t credential[MAX(CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_MAX_LEN,
			       CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID_MAX_LEN)];
	size_t cred_len = CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID_MAX_LEN;

	err = tls_credential_get(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				 TLS_CREDENTIAL_PSK_ID,
				 credential, &cred_len);
	if (err < 0) {
		LOG_WRN("Could not read PSK-ID: %d", err);
		goto finish;
	}

	if (!golioth_psk_id_is_valid(credential, cred_len)) {
		LOG_WRN("Configured PSK-ID is invalid");
		err = -EINVAL;
		goto finish;
	}

	cred_len = CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_MAX_LEN;
	err = tls_credential_get(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				 TLS_CREDENTIAL_PSK,
				 credential, &cred_len);
	if (err < 0) {
		LOG_WRN("Could not read PSK: %d", err);
		goto finish;
	}

	if (!golioth_psk_is_valid(credential, cred_len)) {
		LOG_WRN("Configured PSK is invalid");
		err = -EINVAL;
		goto finish;
	}

finish:
	/* Assume credentials are valid if we can't access them */
	if (err == -EACCES) {
		err = 0;
	}

	return err;
}

static int golioth_check_cert_credentials(void)
{
	size_t cred_len = 0;
	int err = tls_credential_get(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				     TLS_CREDENTIAL_SERVER_CERTIFICATE, NULL, &cred_len);
	if (err == -ENOENT) {
		LOG_WRN("Certificate authentication configured, but no client certificate found");
		goto finish;
	}

	err = tls_credential_get(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY, NULL, &cred_len);
	if (err == -ENOENT) {
		LOG_WRN("Certificate authentication configured, but no private key found");
		goto finish;
	}

	if (err == -EFBIG) {
		/* EFBIG is expected, because we pass in a zero-length, NULL buffer */
		err = 0;
	}

finish:
	return err;
}

static int init_tls_auth_cert(void)
{
	int err;

	err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				 TLS_CREDENTIAL_CA_CERTIFICATE,
				 tls_ca_crt, ARRAY_SIZE(tls_ca_crt));
	if (err < 0) {
		LOG_ERR("Failed to register CA cert: %d", err);
		return err;
	}

	return 0;
}

static int init_tls(void)
{
	if (IS_ENABLED(CONFIG_GOLIOTH_AUTH_METHOD_CERT)) {
		return init_tls_auth_cert();
	}

	return 0;
}

static int client_initialize(struct golioth_client *client)
{
	int err;

	golioth_init(client);

	client->rx_buffer = rx_buffer;
	client->rx_buffer_len = sizeof(rx_buffer);

	client->wakeup = golioth_system_client_wakeup;

	err = golioth_set_proto_coap_dtls(client, sec_tag_list,
					  ARRAY_SIZE(sec_tag_list));
	if (err) {
		LOG_ERR("Failed to set protocol: %d", err);
		return err;
	}

	fds[POLLFD_EVENT].fd = eventfd(0, EFD_NONBLOCK);
	fds[POLLFD_EVENT].events = ZSOCK_POLLIN;

	if (IS_ENABLED(CONFIG_LOG_BACKEND_GOLIOTH)) {
		log_backend_golioth_init(client);
	}

	if (IS_ENABLED(CONFIG_GOLIOTH_RPC)) {
		err = golioth_rpc_init(client);
		if (err) {
			LOG_ERR("Failed to initialize RPC: %d", err);
			return err;
		}
	}

	return 0;
}

static int golioth_system_init(void)
{
	struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
	int err;

	LOG_INF("Initializing");

	err = client_initialize(client);
	if (err) {
		LOG_ERR("Failed to initialize client: %d", err);
		return err;
	}

	init_tls();

	return 0;
}

SYS_INIT(golioth_system_init, APPLICATION,
	 CONFIG_GOLIOTH_SYSTEM_CLIENT_INIT_PRIORITY);

static int client_connect(struct golioth_client *client)
{
	int err;

	err = golioth_connect(client,
			      CONFIG_GOLIOTH_SYSTEM_SERVER_HOST,
			      CONFIG_GOLIOTH_SYSTEM_SERVER_PORT);
	if (err) {
		LOG_ERR("Failed to connect: %d", err);
		return err;
	}

	fds[POLLFD_SOCKET].fd = client->sock;
	fds[POLLFD_SOCKET].events = ZSOCK_POLLIN;

	return 0;
}

static void client_disconnect(struct golioth_client *client)
{
	struct k_work_sync sync;

	(void)golioth_disconnect(client);

	k_work_cancel_delayable_sync(&eventfd_timeout, &sync);
}

K_SEM_DEFINE(sys_client_started, 0, 1);
static struct k_poll_event sys_client_poll_start =
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&sys_client_started, 0);

static void wait_for_client_start(void)
{
	k_poll(&sys_client_poll_start, 1, K_FOREVER);
	sys_client_poll_start.state = K_POLL_STATE_NOT_READY;
}

static void golioth_system_client_main(void *arg1, void *arg2, void *arg3)
{
	struct golioth_client *client = arg1;
	bool event_occurred;
	int timeout;
	int64_t recv_expiry = 0;
	int64_t ping_expiry = 0;
	int64_t golioth_timeout;
	eventfd_t eventfd_value;
	int err;
	int ret;

	while (true) {
		if (client->sock < 0) {
			LOG_DBG("Waiting for client to be started");
			wait_for_client_start();

			/* Flush pending events */
			atomic_clear_bit(&flags, FLAG_RECONNECT);
			(void)eventfd_read(fds[POLLFD_EVENT].fd, &eventfd_value);

			LOG_INF("Starting connect");
			err = client_connect(client);
			if (err) {
				LOG_WRN("Failed to connect: %d", err);
				k_sleep(K_SECONDS(5));
				continue;
			}

			LOG_INF("Client connected!");

			recv_expiry = k_uptime_get() + RECV_TIMEOUT;
			ping_expiry = k_uptime_get() + PING_INTERVAL;
		}

		event_occurred = false;

		golioth_poll_prepare(client, k_uptime_get(), NULL, &golioth_timeout);

		timeout = MIN(recv_expiry, ping_expiry) - k_uptime_get();
		timeout = MIN(timeout, golioth_timeout);

		if (timeout < 0) {
			timeout = 0;
		}

		LOG_DBG("Next timeout: %d", timeout);

		k_work_reschedule(&eventfd_timeout, K_MSEC(timeout));

		ret = zsock_poll(fds, ARRAY_SIZE(fds), -1);

		if (ret < 0) {
			LOG_ERR("Error in poll:%d", errno);
			break;
		}

		if (ret == 0) {
			LOG_DBG("Timeout in poll");
			event_occurred = true;
		}

		if (fds[POLLFD_EVENT].revents) {
			(void)eventfd_read(fds[POLLFD_EVENT].fd,
					   &eventfd_value);
			LOG_DBG("Event in eventfd");
			event_occurred = true;
		}

		if (event_occurred) {
			bool reconnect_request = atomic_test_and_clear_bit(&flags, FLAG_RECONNECT);
			bool stop_request = atomic_test_and_clear_bit(&flags, FLAG_STOP_CLIENT);
			bool receive_timeout = (recv_expiry <= k_uptime_get());

			/*
			 * Reconnect and stop requests are handled similar to recv timeout.
			 */
			if (reconnect_request || receive_timeout || stop_request) {
				if (stop_request) {
					LOG_INF("Stop request");
				} else if (reconnect_request) {
					LOG_INF("Reconnect per request");
				} else {
					LOG_WRN("Receive timeout");
				}

				client_disconnect(client);
				continue;
			}

			if (ping_expiry <= k_uptime_get()) {
				LOG_DBG("Sending PING");
				(void)golioth_ping(client);

				ping_expiry = k_uptime_get() + PING_INTERVAL;
			}
		}

		if (fds[POLLFD_SOCKET].revents) {
			recv_expiry = k_uptime_get() + RECV_TIMEOUT;
			ping_expiry = k_uptime_get() + PING_INTERVAL;

			err = golioth_process_rx(client);
			if (err) {
				LOG_ERR("Failed to receive: %d", err);
				client_disconnect(client);
			}
		}
	}
}

K_THREAD_DEFINE(golioth_system, CONFIG_GOLIOTH_SYSTEM_CLIENT_STACK_SIZE,
		golioth_system_client_main,
		GOLIOTH_SYSTEM_CLIENT_GET(), NULL, NULL,
		CONFIG_GOLIOTH_SYSTEM_CLIENT_THREAD_PRIORITY, 0, 0);

void golioth_system_client_start(void)
{
	int err = 0;
	if (IS_ENABLED(CONFIG_GOLIOTH_AUTH_METHOD_PSK)) {
		err = golioth_check_psk_credentials();
	} else if (IS_ENABLED(CONFIG_GOLIOTH_AUTH_METHOD_CERT)) {
		err = golioth_check_cert_credentials();
	}

	if (err == 0) {
		k_sem_give(&sys_client_started);
	} else {
		LOG_WRN("Error loading TLS credentials, golioth system client was not started: %d",
			err);
	}

}

void golioth_system_client_stop(void)
{
	k_sem_take(&sys_client_started, K_NO_WAIT);

	if (!atomic_test_and_set_bit(&flags, FLAG_STOP_CLIENT)) {
		eventfd_write(fds[POLLFD_EVENT].fd, 1);
	}
}

void golioth_system_client_request_reconnect(void)
{
	if (!atomic_test_and_set_bit(&flags, FLAG_RECONNECT)) {
		eventfd_write(fds[POLLFD_EVENT].fd, 1);
	}
}
