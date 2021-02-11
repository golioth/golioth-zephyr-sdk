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

/* CoAP socket fd */
static int sock;

struct pollfd fds[1];
static int nfds;

static int coap_send(struct coap_packet *p)
{
	int ret;

	LOG_HEXDUMP_DBG(p->data, p->offset, "TX");

	ret = send(sock, p->data, p->offset, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send: %d", -errno);
		return -errno;
	}

	return 0;
}

static int send_ack(struct coap_packet *request)
{
	struct coap_packet reply;
	uint8_t *data;
	int err;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	err = coap_ack_init(&reply, request, data, MAX_COAP_MSG_LEN,
			    COAP_CODE_EMPTY);
	if (err) {
		goto free_data;
	}

	err = coap_send(&reply);

free_data:
	k_free(data);

	return err;
}

static int wait_for_rx(void)
{
	struct coap_packet rx;
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t *data;
	int rcvd;
	uint8_t type;
	int ret = 0;

	if (poll(fds, nfds, 5000) < 0) {
		LOG_ERR("Error in poll:%d", errno);
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	rcvd = recv(sock, data, MAX_COAP_MSG_LEN, MSG_DONTWAIT);
	if (rcvd == 0) {
		ret = -EIO;
		goto end;
	}

	if (rcvd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			ret = 0;
		} else {
			ret = -errno;
		}

		goto end;
	}

	LOG_HEXDUMP_DBG(data, rcvd, "RX");

	ret = coap_packet_parse(&rx, data, rcvd, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Invalid data received");
	}

	type = coap_header_get_type(&rx);
	payload = coap_packet_get_payload(&rx, &payload_len);

	if (type == COAP_TYPE_CON) {
		if (payload_len == 0) {
			LOG_INF("PING received");
		}

		send_ack(&rx);
	}

	if (payload) {
		LOG_HEXDUMP_DBG(payload, payload_len, "Payload");
	}

end:
	k_free(data);

	return ret;
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
	fds[nfds].fd = sock;
	fds[nfds].events = POLLIN;
	nfds++;
}

static int start_coap_client(void)
{
	struct sockaddr_in addr4;
	int proto = IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS) ?
		IPPROTO_DTLS_1_2 : IPPROTO_UDP;
	int ret;

	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(PEER_PORT);

	inet_pton(addr4.sin_family, CONFIG_NET_CONFIG_PEER_IPV4_ADDR,
		  &addr4.sin_addr);

	sock = socket(addr4.sin_family, SOCK_DGRAM, proto);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
	sec_tag_t sec_tag_list[] = {
		PSK_TAG,
	};

	ret = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
			 sec_tag_list, sizeof(sec_tag_list));
	if (ret < 0) {
		LOG_ERR("Failed to set TLS_SEC_TAG_LIST option: %d", errno);
		return -errno;
	}
#endif /* defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS) */

	ret = connect(sock, (struct sockaddr *)&addr4, sizeof(addr4));
	if (ret < 0) {
		LOG_ERR("Cannot connect to UDP remote : %d", errno);
		return -errno;
	}

	prepare_fds();

	return 0;
}

static int send_hello(void)
{
	struct coap_packet request;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON, COAP_TOKEN_MAX_LEN,
			     coap_next_token(), COAP_METHOD_GET,
			     coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
		goto end;
	}

	r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
				      "hello", strlen("hello"));
	if (r < 0) {
		LOG_ERR("Unable add option to request");
		goto end;
	}

	if (!IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		r = coap_packet_append_option(&request, COAP_OPTION_URI_QUERY,
					      "id="NOTLS_ID,
					      sizeof("id="NOTLS_ID) - 1);
		if (r < 0) {
			LOG_ERR("Unable add option to request");
			goto end;
		}
	}

	coap_send(&request);

end:
	k_free(data);

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
		r = send_hello();
		if (r < 0) {
			goto quit;
		}

		k_sleep(K_SECONDS(5));
	}

	LOG_DBG("Done");

quit:
	(void)close(sock);

	LOG_DBG("Quit");
}
