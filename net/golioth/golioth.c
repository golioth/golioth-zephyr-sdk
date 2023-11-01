/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <mbedtls/ssl_ciphersuites.h>
#include <net/golioth.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>

#include <golioth_ciphersuites.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "golioth_utils.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth, CONFIG_GOLIOTH_LOG_LEVEL);

#define GOLIOTH_HELLO		"hello"

/* Use mbedTLS macros which are IANA ciphersuite names prepended with MBEDTLS_ */
#define GOLIOTH_CIPHERSUITE_ENTRY(x) _CONCAT(MBEDTLS_, x)

static int golioth_ciphersuites[] = {
	FOR_EACH_NONEMPTY_TERM(GOLIOTH_CIPHERSUITE_ENTRY, (,), GOLIOTH_CIPHERSUITES)
};

void golioth_init(struct golioth_client *client)
{
	memset(client, 0, sizeof(*client));

	k_mutex_init(&client->lock);
	client->sock = -1;

	golioth_coap_reqs_init(client);
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
		 * set CONFIG_GOLIOTH_HOSTNAME_VERIFICATION_SKIP=y, which disables hostname
		 * verification.
		 */
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
				       COND_CODE_1(CONFIG_GOLIOTH_HOSTNAME_VERIFICATION_SKIP,
						   (NULL, 0),
						   (host, strlen(host) + 1)));
		if (ret < 0) {
			return -errno;
		}
	}

	if (sizeof(golioth_ciphersuites) > 0) {
		ret = zsock_setsockopt(sock, SOL_TLS, TLS_CIPHERSUITE_LIST,
				       golioth_ciphersuites, sizeof(golioth_ciphersuites));
		if (ret < 0) {
			return -errno;
		}
	}

	/* If Connection IDs are enabled, set socket option to send CIDs, but not require that the
	 * server sends one in return.
	 */
#ifdef CONFIG_GOLIOTH_USE_CONNECTION_ID
	int enabled = 1;

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_DTLS_CID, &enabled, sizeof(enabled));
	if (ret < 0) {
		return -errno;
	}
#endif /* CONFIG_GOLIOTH_USE_CONNECTION_ID */

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
		golioth_coap_reqs_on_connect(client);

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
	golioth_coap_reqs_on_disconnect(client);

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
				NULL, 0);
	if (err) {
		return err;
	}

	golioth_coap_req_process_rx(client, &client->rx_packet);

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

int golioth_process_rx(struct golioth_client *client)
{
	int flags = ZSOCK_MSG_DONTWAIT |
		(IS_ENABLED(CONFIG_GOLIOTH_RECV_USE_MSG_TRUNC) ? ZSOCK_MSG_TRUNC : 0);
	int ret;
	int err;

	ret = golioth_recv(client, client->rx_buffer, client->rx_buffer_len,
			   flags);
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

void golioth_poll_prepare(struct golioth_client *client, int64_t now,
			  int *fd, int64_t *timeout)
{
	if (fd) {
		*fd = client->sock;
	}

	if (timeout) {
		*timeout = golioth_coap_reqs_poll_prepare(client, now);
	}
}
