/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/coap.h>
#include <net/golioth.h>
#include <net/socket.h>
#include <random/rand32.h>
#include <stdio.h>
#include <string.h>

#include "coap_utils.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth, CONFIG_GOLIOTH_LOG_LEVEL);

#define COAP_BASIC_HEADER_SIZE	4

#define GOLIOTH_HELLO		"hello"

void golioth_init(struct golioth_client *client)
{
	memset(client, 0, sizeof(*client));

	k_mutex_init(&client->lock);
	client->sock = -1;
}

static int golioth_setsockopt_dtls(struct golioth_client *client, int sock)
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

static int golioth_connect_sockaddr(struct golioth_client *client, int *sock,
				    struct sockaddr *addr, socklen_t addrlen)
{
	int ret;
	int err;

	*sock = zsock_socket(addr->sa_family, SOCK_DGRAM, client->proto);
	if (*sock < 0) {
		return -errno;
	}

	if (client->proto == IPPROTO_DTLS_1_2) {
		err = golioth_setsockopt_dtls(client, *sock);
		if (err) {
			goto close_sock;
		}
	}

	ret = zsock_connect(*sock, addr, addrlen);
	if (ret < 0) {
		err = -errno;
		goto close_sock;
	}

	return 0;

close_sock:
	__golioth_close(*sock);

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
		LOG_DBG(fmt, log_strdup(buf));				\
	} while (0)
#else
#define LOG_SOCKADDR(fmt, addr)
#endif

static int __golioth_connect(struct golioth_client *client, int *sock,
			     const char *host, uint16_t port)
{
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP,
	};
	struct addrinfo *addrs, *addr;
	char port_str[8];
	int ret;
	int err = -ENOENT;

	snprintf(port_str, sizeof(port_str), "%" PRIu16, port);

	ret = zsock_getaddrinfo(host, port_str, &hints, &addrs);
	if (ret < 0) {
		LOG_ERR("Fail to get address (%s %s) %d", log_strdup(host),
			log_strdup(port_str), ret);
		return -EAGAIN;
	}

	for (addr = addrs; addr != NULL; addr = addr->ai_next) {
		LOG_SOCKADDR("Trying addr '%s'", addr->ai_addr);

		err = golioth_connect_sockaddr(client, sock, addr->ai_addr,
					       addr->ai_addrlen);
		if (!err) {
			/* Ready to go */
			break;
		}
	}

	freeaddrinfo(addrs);

	return err;
}

int golioth_connect(struct golioth_client *client, const char *host,
		    uint16_t port)
{
	int sock = -1;
	int err = 0;

	if (client->sock >= 0) {
		err = -EALREADY;
	}

	if (err) {
		return err;
	}

	err = __golioth_connect(client, &sock, host, port);
	if (!err) {
		golioth_lock(client);
		client->sock = sock;
		golioth_unlock(client);

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
	}

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

	err = coap_packet_append_payload_marker(packet);
	if (err) {
		return err;
	}

	msg_iov[0].iov_base = packet->data;
	msg_iov[0].iov_len = packet->offset;

	msg_iov[1].iov_base = payload;
	msg_iov[1].iov_len = payload_len;

	ret = golioth_sendmsg(client, &msg, 0);
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

int golioth_lightdb_get(struct golioth_client *client, const uint8_t *path,
			enum coap_content_format format,
			struct coap_reply *reply, coap_reply_t reply_cb)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (!reply || !reply_cb) {
		return -EINVAL;
	}

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT,
				     format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &packet);
	reply->reply = reply_cb;

	return golioth_send_coap(client, &packet);
}

int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum coap_content_format format,
			uint8_t *data, uint16_t data_len)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_POST, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_CONTENT_FORMAT,
				     format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	return golioth_send_coap_payload(client, &packet, data, data_len);
}

static int golioth_coap_observe_init(struct coap_packet *packet,
				     uint8_t *buffer, size_t buffer_len,
				     const char *path)
{
	int err;

	err = coap_packet_init(packet, buffer, buffer_len,
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_append_option_int(packet, COAP_OPTION_OBSERVE, 0);
	if (err) {
		LOG_ERR("Unable to add observe option");
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(packet, path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	return 0;
}

int golioth_lightdb_observe(struct golioth_client *client, const uint8_t *path,
			    enum coap_content_format format,
			    struct coap_reply *reply, coap_reply_t reply_cb)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (!reply || !reply_cb) {
		return -EINVAL;
	}

	err = golioth_coap_observe_init(&packet, buffer, sizeof(buffer), path);
	if (err) {
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT, format);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &packet);
	reply->reply = reply_cb;

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
	if (err) {
		return err;
	}

	if (client->on_message) {
		client->on_message(client, &client->rx_packet);
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

static int golioth_blockwise_request_next(struct golioth_blockwise_observe_ctx *ctx,
					  const struct coap_packet *response)
{
	struct golioth_client *client = ctx->client;
	struct coap_block_context *block_ctx = &ctx->block_ctx;
	struct coap_packet request;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (coap_header_get_type(response) == COAP_TYPE_CON) {
		err = coap_ack_init(&request, response, buffer, sizeof(buffer),
				    COAP_METHOD_GET);
	} else {
		uint8_t ver;
		uint8_t tkl;
		uint8_t token[COAP_TOKEN_MAX_LEN];

		ver = coap_header_get_version(response);
		tkl = coap_header_get_token(response, token);

		err = coap_packet_init(&request, buffer, sizeof(buffer),
				       ver, COAP_TYPE_CON, tkl, token,
				       COAP_METHOD_GET, coap_next_id());
	}

	if (err) {
		LOG_ERR("Failed to init update block request: %d", err);
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&request, ctx->path);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_block2_option(&request, block_ctx);
	if (err) {
		LOG_ERR("Failed to append block2: %d", err);
		return err;
	}

	LOG_DBG("Request block %d",
		(int) (block_ctx->current /
		       coap_block_size_to_bytes(block_ctx->block_size)));

	return golioth_send_coap(client, &request);
}

static int golioth_blockwise_resource_update(const struct coap_packet *update,
					     struct coap_reply *reply,
					     const struct sockaddr *from)
{
	struct golioth_blockwise_observe_ctx *ctx = reply->user_data;
	struct golioth_client *client = ctx->client;
	struct coap_block_context *block_ctx = &ctx->block_ctx;
	const uint8_t *payload;
	uint16_t payload_len;
	bool truncated = (client->rx_received > client->rx_buffer_len);
	enum coap_block_size truncated_block_size = COAP_BLOCK_1024;
	size_t cur_offset, new_offset;
	int err;

	LOG_DBG("Update on blockwise observe %s", ctx->path);

	payload = coap_packet_get_payload(update, &payload_len);
	if (!payload) {
		LOG_ERR("No payload in CoAP!");
		return -EIO;
	}

	if (truncated) {
		/* Actual received block size, based on payload length */
		truncated_block_size = max_block_size_from_payload_len(payload_len);
	}

	err = coap_update_from_block(update, block_ctx);
	if (err) {
		enum coap_block_size requested_block_size;

		LOG_WRN("Failed to update update block context (%d), reinitializing",
			err);

		requested_block_size = truncated ?
			truncated_block_size :
			golioth_estimated_block_size(client);

		coap_block_transfer_init(block_ctx, requested_block_size, 0);
		err = coap_update_from_block(update, block_ctx);
		if (err) {
			LOG_ERR("Failed to update update block context: %d", err);
			return err;
		}
	}

	if (!block_ctx->total_size) {
		LOG_DBG("Not a blockwise packet");
		ctx->received_cb(ctx, payload, 0, payload_len, true);
		return 0;
	}

	cur_offset = block_ctx->current;

	if (truncated && truncated_block_size < block_ctx->block_size) {
		/*
		 * Reduce block size, so that next requested block will
		 * not exceed receive buffer.
		 */
		block_ctx->block_size = truncated_block_size;

		/*
		 * Update received payload length to be aligned with
		 * requested block size.
		 */
		payload_len = coap_block_size_to_bytes(truncated_block_size);
	}

	new_offset = coap_next_block(update, block_ctx);

	if (new_offset == 0) {
		LOG_DBG("Blockwise transfer is finished!");
		coap_block_transfer_init(block_ctx,
					 golioth_estimated_block_size(client),
					 0);
		ctx->received_cb(ctx, payload, cur_offset, payload_len, true);
		return 0;
	}

	LOG_DBG("Update offset: %zu -> %zu", cur_offset, new_offset);

	err = ctx->received_cb(ctx, payload, cur_offset, payload_len, false);
	if (err) {
		return err;
	}

	return golioth_blockwise_request_next(ctx, update);
}

int golioth_observe_blockwise(struct golioth_client *client,
			      struct golioth_blockwise_observe_ctx *ctx,
			      const char *path, struct coap_reply *reply,
			      golioth_blockwise_observe_received_t received_cb)
{
	struct coap_packet request;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	if (!reply || !received_cb) {
		return -EINVAL;
	}

	ctx->client = client;
	coap_block_transfer_init(&ctx->block_ctx,
				 golioth_estimated_block_size(client), 0);
	ctx->path = path;
	ctx->received_cb = received_cb;

	err = golioth_coap_observe_init(&request, buffer, sizeof(buffer), path);
	if (err) {
		return err;
	}

	err = coap_append_block2_option(&request, &ctx->block_ctx);
	if (err) {
		LOG_ERR("Unable add option to request");
		return err;
	}

	coap_reply_clear(reply);
	coap_reply_init(reply, &request);
	reply->reply = golioth_blockwise_resource_update;
	reply->user_data = ctx;

	return golioth_send_coap(client, &request);
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
