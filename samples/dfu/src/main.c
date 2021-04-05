/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_dfu, LOG_LEVEL_DBG);

#include <errno.h>
#include <logging/golioth.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/golioth.h>
#include <net/tls_credentials.h>
#include <posix/sys/eventfd.h>

#include <dfu/flash_img.h>
#include <dfu/mcuboot.h>
#include <logging/log_ctrl.h>
#include <power/reboot.h>
#include <storage/flash_map.h>

#include "wifi.h"

#define RX_TIMEOUT		K_SECONDS(30)

#define MAX_COAP_MSG_LEN	2048

#define TLS_PSK_ID		CONFIG_GOLIOTH_SERVER_DTLS_PSK_ID
#define TLS_PSK			CONFIG_GOLIOTH_SERVER_DTLS_PSK

#define PSK_TAG			1

#define REBOOT_DELAY_SEC	1

/* Golioth instance */
static struct golioth_client g_client;
static struct golioth_client *client = &g_client;

static uint8_t rx_buffer[MAX_COAP_MSG_LEN];

static struct sockaddr addr;

#define POLLFD_EVENT_RECONNECT	0
#define POLLFD_SOCKET		1

static struct zsock_pollfd fds[2];
static struct coap_reply coap_replies[1];

static K_SEM_DEFINE(golioth_client_ready, 0, 1);

struct dfu_ctx {
	struct golioth_blockwise_observe_ctx observe;
	struct flash_img_context flash;
};

static struct dfu_ctx update_ctx;

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

/**
 * Determines if the specified area of flash is completely unwritten.
 *
 * @note This is a copy of zephyr_img_mgmt_flash_check_empty() from mcumgr.
 */
static int flash_area_check_empty(const struct flash_area *fa,
				  bool *out_empty)
{
	uint32_t data[16];
	off_t addr;
	off_t end;
	int bytes_to_read;
	int rc;
	int i;

	__ASSERT_NO_MSG(fa->fa_size % 4 == 0);

	end = fa->fa_size;
	for (addr = 0; addr < end; addr += sizeof(data)) {
		if (end - addr < sizeof(data)) {
			bytes_to_read = end - addr;
		} else {
			bytes_to_read = sizeof(data);
		}

		rc = flash_area_read(fa, addr, data, bytes_to_read);
		if (rc != 0) {
			flash_area_close(fa);
			return rc;
		}

		for (i = 0; i < bytes_to_read / 4; i++) {
			if (data[i] != 0xffffffff) {
				*out_empty = false;
				flash_area_close(fa);
				return 0;
			}
		}
	}

	*out_empty = true;

	return 0;
}

static int flash_img_erase_if_needed(struct flash_img_context *ctx)
{
	bool empty;
	int err;

	if (IS_ENABLED(CONFIG_IMG_ERASE_PROGRESSIVELY)) {
		return 0;
	}

	err = flash_area_check_empty(ctx->flash_area, &empty);
	if (err) {
		return err;
	}

	if (empty) {
		return 0;
	}

	err = flash_area_erase(ctx->flash_area, 0, ctx->flash_area->fa_size);
	if (err) {
		return err;
	}

	return 0;
}

static const char *swap_type_str(int swap_type)
{
	switch (swap_type) {
	case BOOT_SWAP_TYPE_NONE:
		return "none";
	case BOOT_SWAP_TYPE_TEST:
		return "test";
	case BOOT_SWAP_TYPE_PERM:
		return "perm";
	case BOOT_SWAP_TYPE_REVERT:
		return "revert";
	case BOOT_SWAP_TYPE_FAIL:
		return "fail";
	}

	return "unknown";
}

static int flash_img_prepare(struct flash_img_context *flash)
{
	int swap_type;
	int err;

	swap_type = mcuboot_swap_type();
	switch (swap_type) {
	case BOOT_SWAP_TYPE_REVERT:
		LOG_WRN("'revert' swap type detected, it is not safe to continue");
		return -EBUSY;
	default:
		LOG_INF("swap type: %s", swap_type_str(swap_type));
		break;
	}

	err = flash_img_init(flash);
	if (err) {
		LOG_ERR("failed to init: %d", err);
		return err;
	}

	err = flash_img_erase_if_needed(flash);
	if (err) {
		LOG_ERR("failed to erase: %d", err);
		return err;
	}

	return 0;
}

static int data_received(struct golioth_blockwise_observe_ctx *observe,
			 const uint8_t *data, size_t offset, size_t len,
			 bool last)
{
	struct dfu_ctx *dfu = CONTAINER_OF(observe, struct dfu_ctx, observe);
	int err;

	LOG_DBG("Received %zu bytes at offset %zu%s", len, offset,
		last ? " (last)" : "");

	if (offset == 0) {
		err = flash_img_prepare(&dfu->flash);
		if (err) {
			return err;
		}
	}

	err = flash_img_buffered_write(&dfu->flash, data, len, last);
	if (err) {
		LOG_ERR("Failed to write to flash: %d", err);
		return err;
	}

	if (offset > 0 && last) {
		LOG_INF("Requesting upgrade");

		err = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (err) {
			LOG_ERR("Failed to request upgrade: %d", err);
			return err;
		}

		LOG_INF("Rebooting in %d second(s)", REBOOT_DELAY_SEC);

		/* Synchronize logs */
		LOG_PANIC();

		k_sleep(K_SECONDS(REBOOT_DELAY_SEC));

		sys_reboot(SYS_REBOOT_COLD);
	}

	return 0;
}

static int observe_update(void)
{
	struct coap_reply *reply;
	int err;

	reply = coap_reply_next_unused(coap_replies, ARRAY_SIZE(coap_replies));
	if (!reply) {
		LOG_ERR("No more reply handlers");
		return -ENOMEM;
	}

	err = golioth_observe_blockwise(client, &update_ctx.observe, "update",
					reply, data_received);
	if (err) {
		coap_reply_clear(reply);
	}

	return err;
}

static int connect_client(void)
{
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

	return observe_update();
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

	if (IS_ENABLED(CONFIG_NET_L2_WIFI_MGMT)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

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
	int err;

	LOG_DBG("Start DFU sample");

	err = boot_write_img_confirmed();
	if (err) {
		LOG_ERR("Failed to confirm image: %d", err);
	}

	k_sem_take(&golioth_client_ready, K_FOREVER);

	while (true) {
		err = golioth_send_hello(client);
		if (err) {
			LOG_WRN("Failed to send hello: %d", err);
		}

		k_sleep(K_SECONDS(5));
	}
}
