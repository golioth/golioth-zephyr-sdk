/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/rand32.h>
#include <stdio.h>
#include <string.h>

#include "coap_utils.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth, CONFIG_GOLIOTH_LOG_LEVEL);

#define GOLIOTH_HELLO		"hello"

void golioth_init(struct golioth_client *client)
{
	memset(client, 0, sizeof(*client));

	k_mutex_init(&client->lock);
	client->sock = -1;
}

bool golioth_is_connected(struct golioth_client *client)
{
	bool is_connected;

	k_mutex_lock(&client->lock, K_FOREVER);
	is_connected = (client->sock >= 0);
	k_mutex_unlock(&client->lock);

	return is_connected;
}

static int golioth_setsockopt_dtls(struct golioth_client *client, int sock,
				   const char *host)
{
	int ret;

	if (client->tls.sec_tag_list && client->tls.sec_tag_count) {
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
				client->tls.sec_tag_list,
				client->tls.sec_tag_count *
					sizeof(*client->tls.sec_tag_list));
		if (ret < 0) {
			return -errno;
		}
	}

	if (IS_ENABLED(CONFIG_GOLIOTH_HOSTNAME_VERIFICATION)) {
		/*
		 * NOTE: At the time of implementation, mbedTLS supported only DNS entries in X509
		 * Subject Alternative Name, so providing string representation of IP address will
		 * fail (during handshake). If this is the case, you can can still connect if you
		 * modify the code below to set host to NULL, which disables hostname verification.
		 *
		 * NOTE: Zephyr TLS layer / mbedTLS API expect NULL terminated string. Length
		 * (calculated with 'strlen') is ignored at Zephyr TLS layer. Though, we provide
		 * length without NULL termination, just in case Zephyr/mbedTLS implementation would
		 * change.
		 */
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host));
		if (ret < 0) {
			return -errno;
		}
	}

	return 0;
}

static int __golioth_close(int sock)
{
	int ret;

	if (sock < 0) {
		return -ENOTCONN;
	}

	ret = zsock_close(sock);

	if (ret < 0) {
		return -errno;
	}

	return 0;
}

static int __golioth_send(struct golioth_client *client, uint8_t *data,
			  size_t len, int flags)
{
	ssize_t sent;

	if (client->sock < 0) {
		return -ENOTCONN;
	}

	sent = zsock_send(client->sock, data, len, flags);
	if (sent < 0) {
		return -errno;
	} else if (sent < len) {
		return -EIO;
	}

	return 0;
}

static int __golioth_send_empty_coap(struct golioth_client *client)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_EMPTY_PACKET_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_NON_CON,
			       0, NULL,
			       COAP_CODE_EMPTY, coap_next_id());
	if (err) {
		return err;
	}

	return __golioth_send(client, packet.data, packet.offset, 0);
}

static int golioth_connect_sockaddr(struct golioth_client *client, const char *host,
				    struct sockaddr *addr, socklen_t addrlen)
{
	int sock;
	int ret;
	int err = 0;

	sock = zsock_socket(addr->sa_family, SOCK_DGRAM, client->proto);
	if (sock < 0) {
		return -errno;
	}

	err = golioth_setsockopt_dtls(client, sock, host);
	if (err) {
		goto close_sock;
	}

	ret = zsock_connect(sock, addr, addrlen);
	if (ret < 0) {
		err = -errno;
		goto close_sock;
	}

	golioth_lock(client);
	client->sock = sock;

	/* Send empty packet to start TLS handshake */
	err = __golioth_send_empty_coap(client);
	if (err) {
		client->sock = -1;
		goto unlock;
	}

unlock:
	golioth_unlock(client);

close_sock:
	if (err) {
		__golioth_close(sock);
	}

	return err;
}

#if CONFIG_GOLIOTH_LOG_LEVEL >= LOG_LEVEL_DBG
#define LOG_SOCKADDR(fmt, addr)						\
	do {								\
		char buf[NET_IPV6_ADDR_LEN];				\
									\
		if (addr->sa_family == AF_INET6) {			\
			net_addr_ntop(AF_INET6, &net_sin6(addr)->sin6_addr, \
				      buf, sizeof(buf));		\
		} else if (addr->sa_family == AF_INET) {		\
			net_addr_ntop(AF_INET, &net_sin(addr)->sin_addr, \
				      buf, sizeof(buf));		\
		}							\
									\
		LOG_DBG(fmt, buf);					\
	} while (0)
#else
#define LOG_SOCKADDR(fmt, addr)
#endif

static int __golioth_connect(struct golioth_client *client,
			     const char *host, uint16_t port)
{
	struct zsock_addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP,
	};
	struct zsock_addrinfo *addrs, *addr;
	char port_str[8];
	int ret;
	int err = -ENOENT;

	snprintf(port_str, sizeof(port_str), "%" PRIu16, port);

	ret = zsock_getaddrinfo(host, port_str, &hints, &addrs);
	if (ret < 0) {
		LOG_ERR("Fail to get address (%s %s) %d", host, port_str, ret);
		return -EAGAIN;
	}

	for (addr = addrs; addr != NULL; addr = addr->ai_next) {
		LOG_SOCKADDR("Trying addr '%s'", addr->ai_addr);

		err = golioth_connect_sockaddr(client, host, addr->ai_addr, addr->ai_addrlen);
		if (!err) {
			/* Ready to go */
			break;
		}
	}

	zsock_freeaddrinfo(addrs);

	return err;
}

int golioth_connect(struct golioth_client *client, const char *host,
		    uint16_t port)
{
	int err;

	if (client->sock >= 0) {
		return -EALREADY;
	}

	err = __golioth_connect(client, host, port);
	if (!err) {
		if (client->on_connect) {
			client->on_connect(client);
		}
	}

	return err;
}

static int golioth_close(struct golioth_client *client)
{
	int ret;

	golioth_lock(client);
	ret = __golioth_close(client->sock);
	client->sock = -1;
	golioth_unlock(client);

	return ret;
}

int golioth_disconnect(struct golioth_client *client)
{
	return golioth_close(client);
}

int golioth_set_proto_coap_dtls(struct golioth_client *client,
				sec_tag_t *sec_tag_list,
				size_t sec_tag_count)
{
	if (!sec_tag_list || !sec_tag_count) {
		return -EINVAL;
	}

	client->proto = IPPROTO_DTLS_1_2;
	client->tls.sec_tag_list = sec_tag_list;
	client->tls.sec_tag_count = sec_tag_count;

	return 0;
}

static int golioth_send(struct golioth_client *client, uint8_t *data,
			size_t len, int flags)
{
	int ret;

	golioth_lock(client);
	ret = __golioth_send(client, data, len, flags);
	golioth_unlock(client);

	return ret;
}

static void *msg_linearize(const struct msghdr *msg, size_t *total_len)
{
	struct iovec *iovec_begin = msg->msg_iov;
	struct iovec *iovec_end = iovec_begin + msg->msg_iovlen;
	struct iovec *iovec;
	uint8_t *buffer, *p;
	size_t len = 0;

	for (iovec = iovec_begin; iovec < iovec_end; iovec++) {
		len += iovec->iov_len;
	}

	buffer = malloc(len);
	if (!buffer) {
		return NULL;
	}

	p = buffer;
	for (iovec = iovec_begin; iovec < iovec_end; iovec++) {
		memcpy(p, iovec->iov_base, iovec->iov_len);
		p += iovec->iov_len;
	}

	*total_len = len;

	return buffer;
}

static int golioth_sendmsg(struct golioth_client *client,
			   const struct msghdr *msg, int flags)
{
	uint8_t *data;
	size_t len;
	int ret;

	data = msg_linearize(msg, &len);
	if (!data) {
		return -ENOMEM;
	}

	golioth_lock(client);
	ret = __golioth_send(client, data, len, flags);
	golioth_unlock(client);

	free(data);

	return ret;
}

int golioth_send_coap(struct golioth_client *client, struct coap_packet *packet)
{
	int err;

	err = golioth_send(client, packet->data, packet->offset, 0);
	if (err) {
		return err;
	}

	return 0;
}

int golioth_send_coap_payload(struct golioth_client *client,
			      struct coap_packet *packet,
			      uint8_t *payload, uint16_t payload_len)
{
	struct iovec msg_iov[2] = {};
	struct msghdr msg = {
		.msg_iov = msg_iov,
		.msg_iovlen = ARRAY_SIZE(msg_iov),
	};
	int err;

	err = coap_packet_append_payload_marker(packet);
	if (err) {
		return err;
	}

	msg_iov[0].iov_base = packet->data;
	msg_iov[0].iov_len = packet->offset;

	msg_iov[1].iov_base = payload;
	msg_iov[1].iov_len = payload_len;

	err = golioth_sendmsg(client, &msg, 0);
	if (err) {
		return err;
	}

	return 0;
}

int golioth_ping(struct golioth_client *client)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_EMPTY_PACKET_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       0, NULL,
			       COAP_CODE_EMPTY, coap_next_id());
	if (err) {
		return err;
	}

	return golioth_send_coap(client, &packet);
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
	if (err) {
		return err;
	}

	if (client->on_message) {
		client->on_message(client, &client->rx_packet);
	}

	for (size_t i = 0; i < client->num_message_callbacks; i++) {
		const struct golioth_message_callback_reg *callback_reg =
			&client->message_callbacks[i];
		if (callback_reg->callback) {
			callback_reg->callback(client, &client->rx_packet, callback_reg->user_arg);
		}
	}

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

static int __golioth_recv(struct golioth_client *client, uint8_t *data,
			  size_t len, int flags)
{
	ssize_t rcvd;

	if (client->sock < 0) {
		return -ENOTCONN;
	}

	rcvd = zsock_recv(client->sock, data, len, flags);
	if (rcvd < 0) {
		return -errno;
	} else if (rcvd == 0) {
		return -ENOTCONN;
	}

	return rcvd;
}

static int golioth_recv(struct golioth_client *client, uint8_t *data,
			size_t len, int flags)
{
	int ret;

	golioth_lock(client);
	ret = __golioth_recv(client, data, len, flags);
	golioth_unlock(client);

	return ret;
}

static enum coap_block_size max_block_size_from_payload_len(uint16_t payload_len)
{
	enum coap_block_size block_size = COAP_BLOCK_16;

	payload_len /= 16;

	while (payload_len > 1 && block_size < COAP_BLOCK_1024) {
		block_size++;
		payload_len /= 2;
	}

	return block_size;
}

static enum coap_block_size
golioth_estimated_block_size(struct golioth_client *client)
{
	return max_block_size_from_payload_len(client->rx_buffer_len);
}

void golioth_blockwise_download_init(struct golioth_client *client,
				     struct golioth_blockwise_download_ctx *ctx)
{
	ctx->client = client;
	coap_block_transfer_init(&ctx->block_ctx,
				 golioth_estimated_block_size(client), 0);

	sys_put_be32(sys_rand32_get(), &ctx->token[0]);
	sys_put_be32(sys_rand32_get(), &ctx->token[4]);
}

int golioth_process_rx(struct golioth_client *client)
{
	int ret;
	int err;

	ret = golioth_recv(client, client->rx_buffer, client->rx_buffer_len,
			   ZSOCK_MSG_DONTWAIT | ZSOCK_MSG_TRUNC);
	if (ret == -EAGAIN || ret == -EWOULDBLOCK) {
		/* no pending data */
		return 0;
	} else if (ret < 0) {
		return ret;
	}

	client->rx_received = ret;

	if (ret > client->rx_buffer_len) {
		LOG_WRN("Truncated packet (%zu -> %zu)", (size_t) ret,
			client->rx_buffer_len);
		ret = client->rx_buffer_len;
	}

	err = coap_data_check_rx_packet_type(client->rx_buffer, ret);
	if (err == -ENOMSG) {
		/* ping */
		return golioth_process_rx_ping(client, client->rx_buffer, ret);
	} else if (err) {
		return err;
	}

	return golioth_process_rx_data(client, client->rx_buffer, ret);
}

int golioth_register_message_callback(struct golioth_client *client,
				      golioth_message_callback callback,
				      void *user_arg)
{
	if (client->num_message_callbacks >= GOLIOTH_MAX_NUM_MESSAGE_CALLBACKS) {
		LOG_ERR("No more message callback registration slots");
		return -ENOBUFS;
	}

	struct golioth_message_callback_reg *new_reg =
		&client->message_callbacks[client->num_message_callbacks++];
	new_reg->callback = callback;
	new_reg->user_arg = user_arg;

	return 0;
}
