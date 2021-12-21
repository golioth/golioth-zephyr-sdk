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
#include <settings/settings.h>
#include <sys/atomic.h>

#define USE_EVENTFD							\
	IS_ENABLED(CONFIG_GOLIOTH_SYSTEM_CLIENT_TIMEOUT_USING_EVENTFD)

#define RX_BUFFER_SIZE		CONFIG_GOLIOTH_SYSTEM_CLIENT_RX_BUF_SIZE

#ifdef CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID
#define TLS_PSK_ID		CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID
#else
#define TLS_PSK_ID		""
#endif

#ifdef CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK
#define TLS_PSK			CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK
#else
#define TLS_PSK			""
#endif

#ifdef CONFIG_MBEDTLS_PSK_MAX_LEN
#define PSK_MAX_LEN		CONFIG_MBEDTLS_PSK_MAX_LEN
BUILD_ASSERT(sizeof(TLS_PSK) - 1 <= CONFIG_MBEDTLS_PSK_MAX_LEN,
	     "PSK exceeds mbedTLS configured maximum PSK length");
#else
/*
 * Support NCS mirror of Zephyr, which does not have CONFIG_MBEDTLS_PSK_MAX_LEN
 * defined yet.
 */
#define PSK_MAX_LEN		64
#endif

#define PSK_TAG			1

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
	PSK_TAG,
};

enum {
	FLAG_RECONNECT,
};

static atomic_t flags;

static inline void client_request_reconnect(void)
{
	if (!atomic_test_and_set_bit(&flags, FLAG_RECONNECT)) {
		if (USE_EVENTFD) {
			eventfd_write(fds[POLLFD_EVENT].fd, 1);
		}
	}
}

static inline void client_notify_timeout(void)
{
	if (USE_EVENTFD) {
		eventfd_write(fds[POLLFD_EVENT].fd, 1);
	}
}

static void eventfd_timeout_handle(struct k_work *work)
{
	client_notify_timeout();
}

static K_WORK_DELAYABLE_DEFINE(eventfd_timeout, eventfd_timeout_handle);

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

	if (USE_EVENTFD) {
		fds[POLLFD_EVENT].fd = eventfd(0, EFD_NONBLOCK);
		fds[POLLFD_EVENT].events = ZSOCK_POLLIN;
	}

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

	err = client_initialize(client);
	if (err) {
		LOG_ERR("Failed to initialize client: %d", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		if (IS_ENABLED(CONFIG_GOLIOTH_SYSTEM_SETTINGS)) {
			err = settings_subsys_init();
			if (err) {
				LOG_ERR("Failed to initialize settings subsystem: %d", err);
				return err;
			}
		} else {
			init_tls();
		}
	}

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
	(void)golioth_disconnect(client);

	if (USE_EVENTFD) {
		struct k_work_sync sync;

		k_work_cancel_delayable_sync(&eventfd_timeout, &sync);
	}
}

static void golioth_system_client_main(void *arg1, void *arg2, void *arg3)
{
	struct golioth_client *client = arg1;
	bool timeout_occurred;
	int timeout;
	int64_t recv_expiry = 0;
	int64_t ping_expiry = 0;
	eventfd_t eventfd_value;
	int err;
	int ret;

	while (true) {
		if (client->sock < 0) {
			LOG_DBG("Waiting for client to be started");
			wait_for_client_start();

			LOG_INF("Starting connect");
			err = client_connect(client);
			if (err) {
				LOG_WRN("Failed to connect: %d", err);
				k_sleep(K_SECONDS(5));
				continue;
			}

			if (USE_EVENTFD) {
				/* Flush reconnect requests */
				(void)eventfd_read(fds[POLLFD_EVENT].fd,
						   &eventfd_value);
			}

			LOG_INF("Client connected!");

			recv_expiry = k_uptime_get() + RECV_TIMEOUT;
			ping_expiry = k_uptime_get() + PING_INTERVAL;
		}

		timeout_occurred = false;

		timeout = MIN(recv_expiry, ping_expiry) - k_uptime_get();
		if (timeout < 0) {
			timeout = 0;
		}

		LOG_DBG("Next timeout: %d", timeout);

		if (USE_EVENTFD) {
			k_work_reschedule(&eventfd_timeout, K_MSEC(timeout));

			ret = zsock_poll(fds, ARRAY_SIZE(fds), -1);
		} else {
			ret = zsock_poll(&fds[POLLFD_SOCKET], 1, timeout);
		}

		if (ret < 0) {
			LOG_ERR("Error in poll:%d", errno);
			break;
		}

		if (ret == 0) {
			LOG_DBG("Timeout in poll");
			timeout_occurred = true;
		}

		if (USE_EVENTFD && fds[POLLFD_EVENT].revents) {
			(void)eventfd_read(fds[POLLFD_EVENT].fd,
					   &eventfd_value);
			LOG_DBG("Timeout in eventfd");
			timeout_occurred = true;
		}

		if (timeout_occurred) {
			bool reconnect_request = atomic_test_and_clear_bit(&flags, FLAG_RECONNECT);
			bool receive_timeout = (recv_expiry <= k_uptime_get());

			/*
			 * Reconnect request is handled similar to recv timeout.
			 */
			if (reconnect_request || receive_timeout) {
				if (reconnect_request) {
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

K_THREAD_DEFINE(golioth_system, 2048, golioth_system_client_main,
		GOLIOTH_SYSTEM_CLIENT_GET(), NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

void golioth_system_client_start(void)
{
	k_sem_give(&sys_client_started);
}

void golioth_system_client_stop(void)
{
	k_sem_take(&sys_client_started,K_NO_WAIT);
}

#if defined(CONFIG_GOLIOTH_SYSTEM_SETTINGS) &&	\
	defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)

/*
 * TLS credentials subsystem just remembers pointers to memory areas where
 * credentials are stored. This means that we need to allocate memory for
 * credentials ourselves.
 */
static uint8_t golioth_dtls_psk[PSK_MAX_LEN];
static size_t golioth_dtls_psk_len;
static uint8_t golioth_dtls_psk_id[64];
static size_t golioth_dtls_psk_id_len;

static int golioth_settings_get(const char *name, char *dst, int val_len_max)
{
	uint8_t *val;
	size_t val_len;

	if (!strcmp(name, "psk")) {
		val = golioth_dtls_psk;
		val_len = strlen(golioth_dtls_psk);
	} else if (!strcmp(name, "psk-id")) {
		val = golioth_dtls_psk_id;
		val_len = strlen(golioth_dtls_psk_id);
	} else {
		LOG_WRN("Unsupported key '%s'", log_strdup(name));
		return -ENOENT;
	}

	if (val_len > val_len_max) {
		LOG_ERR("Not enough space (%zu %d)", val_len, val_len_max);
		return -ENOMEM;
	}

	memcpy(dst, val, val_len);

	return val_len;
}

static int golioth_settings_set(const char *name, size_t len_rd,
				settings_read_cb read_cb, void *cb_arg)
{
	enum tls_credential_type type;
	uint8_t *value;
	size_t *value_len;
	size_t buffer_len;
	ssize_t ret;
	int err;

	if (!strcmp(name, "psk")) {
		type = TLS_CREDENTIAL_PSK;
		value = golioth_dtls_psk;
		value_len = &golioth_dtls_psk_len;
		buffer_len = sizeof(golioth_dtls_psk);
	} else if (!strcmp(name, "psk-id")) {
		type = TLS_CREDENTIAL_PSK_ID;
		value = golioth_dtls_psk_id;
		value_len = &golioth_dtls_psk_id_len;
		buffer_len = sizeof(golioth_dtls_psk_id);
	} else {
		LOG_ERR("Unsupported key '%s'", log_strdup(name));
		return -ENOTSUP;
	}

	if (IS_ENABLED(CONFIG_SETTINGS_RUNTIME)) {
		err = tls_credential_delete(PSK_TAG, type);
		if (err && err != -ENOENT) {
			LOG_ERR("Failed to delete cred %s: %d",
				log_strdup(name), err);
			return err;
		}
	}

	ret = read_cb(cb_arg, value, buffer_len);
	if (ret < 0) {
		LOG_ERR("Failed to read value: %d", (int) ret);
		return ret;
	}

	*value_len = ret;

	LOG_DBG("Name: %s", log_strdup(name));
	LOG_HEXDUMP_DBG(value, *value_len, "value");

	err = tls_credential_add(PSK_TAG, type, value, *value_len);
	if (err) {
		LOG_ERR("Failed to add cred %s: %d", log_strdup(name), err);
		return err;
	}

	client_request_reconnect();

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(golioth, "golioth",
	IS_ENABLED(CONFIG_SETTINGS_RUNTIME) ? golioth_settings_get : NULL,
	golioth_settings_set, NULL, NULL);

#endif /* defined(CONFIG_GOLIOTH_SYSTEM_SETTINGS) &&
	* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
