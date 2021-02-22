/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2021 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_hello, LOG_LEVEL_DBG);

#include <errno.h>
#include <net/socket.h>
#include <net/coap.h>
#include <net/golioth.h>
#include <net/tls_credentials.h>

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
#define PEER_PORT		5684
#else
#define PEER_PORT		5683
#endif
#define MAX_COAP_MSG_LEN	256

#define TLS_PSK_ID		"mark-one-id"
#define TLS_PSK			"1r0nm@n"
#define NOTLS_ID		"mark-one"

#define PSK_TAG			1

/* Golioth instance */
static struct golioth_client g_client;
static struct golioth_client *client = &g_client;

static uint8_t rx_buffer[MAX_COAP_MSG_LEN];

struct pollfd fds[1];
static int nfds;

static int wait_for_rx(void)
{
	if (poll(fds, nfds, 5000) < 0) {
		LOG_ERR("Error in poll:%d", errno);
	}

	return golioth_process_rx(client);
}

static void coap_receive(void *arg1, void *arg2, void *arg3)
{
	int err;

	while (true) {
		err = wait_for_rx();
		if (err < 0) {
			LOG_ERR("Failed to receive: %d", err);
		}
	}
}

K_THREAD_DEFINE(coap_rx, 1024, coap_receive, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, -1);

static void prepare_fds(void)
{
	fds[nfds].fd = client->sock;
	fds[nfds].events = POLLIN;
	nfds++;
}

static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t type;

	type = coap_header_get_type(rx);
	payload = coap_packet_get_payload(rx, &payload_len);

	if (payload) {
		LOG_HEXDUMP_DBG(payload, payload_len, "Payload");
	}
}

static int start_coap_client(void)
{
	struct sockaddr_in addr4;
	sec_tag_t sec_tag_list[] = {
#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
		PSK_TAG,
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */
	};
	int err;

	golioth_init(client);

	client->rx_buffer = rx_buffer;
	client->rx_buffer_len = sizeof(rx_buffer);

	client->on_message = golioth_on_message;

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		err = golioth_set_proto_coap_dtls(client, sec_tag_list,
						  ARRAY_SIZE(sec_tag_list));
	} else {
		err = golioth_set_proto_coap_udp(client, NOTLS_ID,
						 sizeof(NOTLS_ID) - 1);
	}
	if (err) {
		LOG_ERR("Failed to set protocol: %d", err);
		return err;
	}

	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(PEER_PORT);

	inet_pton(addr4.sin_family, CONFIG_NET_CONFIG_PEER_IPV4_ADDR,
		  &addr4.sin_addr);

	client->server = (struct sockaddr *)&addr4;

	err = golioth_connect(client);
	if (err) {
		LOG_ERR("Failed to connect: %d", err);
		return err;
	}

	prepare_fds();

	return 0;
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

void main(void)
{
	int r;

	if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		init_tls();
	}

	LOG_DBG("Start CoAP-client sample");

	r = start_coap_client();
	if (r < 0) {
		goto quit;
	}

	k_thread_start(coap_rx);

	while (true) {
		r = golioth_send_hello(client);
		if (r < 0) {
			goto disconnect;
		}

		k_sleep(K_SECONDS(5));
	}

	LOG_DBG("Done");

disconnect:
	golioth_disconnect(client);

quit:
	LOG_DBG("Quit");
}
