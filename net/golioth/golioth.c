/*
 * Copyright (c) 2021 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/coap.h>
#include <net/golioth.h>
#include <net/socket.h>
#include <string.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth, CONFIG_NET_GOLIOTH_LOG_LEVEL);

#define COAP_BASIC_HEADER_SIZE	4

#define GOLIOTH_HELLO		"hello"

void golioth_init(struct golioth_client *client)
{
	memset(client, 0, sizeof(*client));
}

static int golioth_setsockopt_dtls(struct golioth_client *client)
{
	int ret;

	if (client->tls.sec_tag_list && client->tls.sec_tag_count) {
		ret = zsock_setsockopt(client->sock, SOL_TLS, TLS_SEC_TAG_LIST,
				client->tls.sec_tag_list,
				client->tls.sec_tag_count *
					sizeof(*client->tls.sec_tag_list));
		if (ret < 0) {
			return -errno;
		}
	}

	return 0;
}

static int golioth_connect_sock(struct golioth_client *client)
{
	size_t addr_size = sizeof(struct sockaddr_in6);
	int ret;

	if (client->server->sa_family == AF_INET) {
		addr_size = sizeof(struct sockaddr_in);
	}

	ret = zsock_connect(client->sock, client->server, addr_size);
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

int golioth_connect(struct golioth_client *client)
{
	int err;

	client->sock = zsock_socket(client->server->sa_family, SOCK_DGRAM,
				    client->proto);
	if (client->sock < 0) {
		return -errno;
	}

	if (client->proto == IPPROTO_DTLS_1_2) {
		err = golioth_setsockopt_dtls(client);
		if (err) {
			goto close_sock;
		}
	}

	err = golioth_connect_sock(client);
	if (err) {
		goto close_sock;
	}

	return 0;

close_sock:
	(void)zsock_close(client->sock);

	return err;
}

int golioth_disconnect(struct golioth_client *client)
{
	int ret;

	ret = zsock_close(client->sock);
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

int golioth_set_proto_coap_udp(struct golioth_client *client,
			       uint8_t *identity, size_t identity_len)
{
	client->proto = IPPROTO_UDP;
	client->unsecure.identity = identity;
	client->unsecure.identity_len = identity_len;

	return 0;
}

int golioth_set_proto_coap_dtls(struct golioth_client *client,
				sec_tag_t *sec_tag_list,
				size_t sec_tag_count)
{
	if (!IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
		return -ENOTSUP;
	}

	if (!sec_tag_list || !sec_tag_count) {
		return -EINVAL;
	}

	client->proto = IPPROTO_DTLS_1_2;
	client->tls.sec_tag_list = sec_tag_list;
	client->tls.sec_tag_count = sec_tag_count;

	return 0;
}

#define QUERY_PREFIX		"id="
#define QUERY_PREFIX_LEN	(sizeof(QUERY_PREFIX) - 1)

static int golioth_send_coap(struct golioth_client *client,
			     struct coap_packet *packet)
{
	int ret;
	int err;

	if (client->proto == IPPROTO_UDP) {
		uint8_t query[QUERY_PREFIX_LEN +
			      GOLIOTH_MAX_IDENTITY_LEN] = QUERY_PREFIX;

		memcpy(query + QUERY_PREFIX_LEN, client->unsecure.identity,
		       client->unsecure.identity_len);

		err = coap_packet_append_option(packet, COAP_OPTION_URI_QUERY,
					query, QUERY_PREFIX_LEN +
						client->unsecure.identity_len);
		if (err) {
			LOG_ERR("Unable add option to packet");
			return err;
		}
	}

	LOG_HEXDUMP_DBG(packet->data, packet->offset, "TX CoAP");

	ret = zsock_send(client->sock, packet->data, packet->offset, 0);
	if (ret < 0) {
		LOG_ERR("Failed to send CoAP: %d", -errno);
		return -errno;
	}

	return 0;
}

int golioth_send_hello(struct golioth_client *client)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_EMPTY_PACKET_LEN + sizeof(GOLIOTH_HELLO) - 1];
	int err;

	LOG_DBG("Send Hello");

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_option(&packet, COAP_OPTION_URI_PATH,
					"hello", sizeof("hello") - 1);
	if (err < 0) {
		LOG_ERR("Unable add option to packet");
		return err;
	}

	return golioth_send_coap(client, &packet);
}

static uint8_t coap_data_get_token_len(uint8_t *data)
{
	return data[0] & 0x0f;
}

static uint8_t coap_data_get_type(uint8_t *data)
{
	return (data[0] & 0x30) >> 4;
}

static uint8_t coap_data_get_code(uint8_t *data)
{
	return data[1];
}

/**
 * Check CoAP packet type based on raw data received.
 *
 * @retval -EINVAL invalid message
 * @retval -ENOMSG empty CoAP message (ping)
 * @retval 0 valid CoAP packet (to be parsed with)
 */
static int coap_data_check_rx_packet_type(uint8_t *data, size_t len)
{
	uint8_t tkl;

	if (!data) {
		return -EINVAL;
	}

	if (len < COAP_BASIC_HEADER_SIZE) {
		return -EINVAL;
	}

	/* Token lengths 9-15 are reserved. */
	tkl = coap_data_get_token_len(data);
	if (tkl > 8) {
		LOG_DBG("Invalid RX");
		return -EINVAL;
	}

	if (tkl == 0 &&
	    len == COAP_BASIC_HEADER_SIZE &&
	    coap_data_get_type(data) == COAP_TYPE_CON &&
	    coap_data_get_code(data) == COAP_CODE_EMPTY) {
		/* Empty packet */
		LOG_DBG("RX Empty");
		return -ENOMSG;
	}

	LOG_DBG("RX Non-empty");

	return 0;
}

static int golioth_ack_packet(struct golioth_client *client,
			      struct coap_packet *rx)
{
	struct coap_packet tx;
	uint8_t buffer[GOLIOTH_EMPTY_PACKET_LEN + COAP_TOKEN_MAX_LEN];
	int err;

	err = coap_ack_init(&tx, rx, buffer, sizeof(buffer), COAP_CODE_EMPTY);
	if (err) {
		return err;
	}

	return golioth_send_coap(client, &tx);
}

static int golioth_process_rx_data(struct golioth_client *client,
				   uint8_t *data, size_t len)
{
	int err;
	uint8_t type;

	err = coap_packet_parse(&client->rx_packet, data, len,
				client->rx_options,
				ARRAY_SIZE(client->rx_options));

	client->on_message(client, &client->rx_packet);

	type = coap_header_get_type(&client->rx_packet);
	if (type == COAP_TYPE_CON) {
		golioth_ack_packet(client, &client->rx_packet);
	}

	return 0;
}

static int golioth_process_rx_ping(struct golioth_client *client,
				   uint8_t *data, size_t len)
{
	int err;

	err = coap_packet_parse(&client->rx_packet, data, len, NULL, 0);
	if (err) {
		return err;
	}

	return golioth_ack_packet(client, &client->rx_packet);
}

int golioth_process_rx(struct golioth_client *client)
{
	ssize_t rcvd;
	int err;

	rcvd = zsock_recv(client->sock, client->rx_buffer,
			  client->rx_buffer_len, MSG_DONTWAIT);
	if (rcvd == 0) {
		return -EIO;
	} else if (rcvd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/* no pending data */
			return 0;
		} else {
			return -errno;
		}
	}

	err = coap_data_check_rx_packet_type(client->rx_buffer, rcvd);
	if (err == -ENOMSG) {
		/* ping */
		return golioth_process_rx_ping(client, client->rx_buffer, rcvd);
	} else if (err) {
		return err;
	}

	return golioth_process_rx_data(client, client->rx_buffer, rcvd);
}
