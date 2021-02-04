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

#define PEER_PORT		5683
#define MAX_COAP_MSG_LEN	256

/* CoAP socket fd */
static int sock;

struct pollfd fds[1];
static int nfds;

static int wait_for_reply(void)
{
	struct coap_packet reply;
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t *data;
	int rcvd;
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

	LOG_HEXDUMP_DBG(data, rcvd, "Response");

	ret = coap_packet_parse(&reply, data, rcvd, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Invalid data received");
	}

	payload = coap_packet_get_payload(&reply, &payload_len);
	if (!payload) {
		goto end;
	}

	LOG_HEXDUMP_DBG(payload, payload_len, "Payload");

end:
	k_free(data);

	return ret;
}

static void coap_receive(void *arg1, void *arg2, void *arg3)
{
	int err;

	while (true) {
		err = wait_for_reply();
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
	int ret = 0;
	struct sockaddr_in addr4;

	addr4.sin_family = AF_INET;
	addr4.sin_port = htons(PEER_PORT);

	inet_pton(addr4.sin_family, CONFIG_NET_CONFIG_PEER_IPV4_ADDR,
		  &addr4.sin_addr);

	sock = socket(addr4.sin_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

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

	r = coap_packet_append_option(&request, COAP_OPTION_URI_QUERY,
				      "id=mark-one", strlen("id=mark-one"));
	if (r < 0) {
		LOG_ERR("Unable add option to request");
		goto end;
	}

	LOG_HEXDUMP_DBG(request.data, request.offset, "Request");

	r = send(sock, request.data, request.offset, 0);

end:
	k_free(data);

	return 0;
}

void main(void)
{
	int r;

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
