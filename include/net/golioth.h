/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_GOLIOTH_H_
#define ZEPHYR_INCLUDE_NET_GOLIOTH_H_

#include <kernel.h>
#include <net/coap.h>
#include <net/tls_credentials.h>
#include <stdint.h>

#define GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN	128

#define GOLIOTH_MAX_IDENTITY_LEN	32
#define GOLIOTH_EMPTY_PACKET_LEN	(16 + GOLIOTH_MAX_IDENTITY_LEN)

#define GOLIOTH_LIGHTDB_PATH(x)		".d/" x

/**
 * @brief UDP (unsecure) credentials (identity only) of Golioth client.
 */
struct golioth_unsecure {
	const uint8_t *identity;
	size_t identity_len;
};

/**
 * @brief (D)TLS credentials of Golioth client.
 */
struct golioth_tls {
	sec_tag_t *sec_tag_list;
	size_t sec_tag_count;
};

/**
 * @brief Represents a Golioth client instance.
 */
struct golioth_client {
	int proto;
	const struct sockaddr *server;

	union {
		struct golioth_unsecure unsecure;
		struct golioth_tls tls;
	};

	uint8_t *rx_buffer;
	size_t rx_buffer_len;
	size_t rx_received;

	struct coap_packet rx_packet;
	struct coap_option rx_options[CONFIG_NET_GOLIOTH_COAP_MAX_OPTIONS];

	struct k_mutex lock;
	int sock;

	void (*on_message)(struct golioth_client *client,
			   struct coap_packet *rx);
};

struct golioth_blockwise_observe_ctx;

/**
 * @typedef golioth_blockwise_observe_received_t
 * @brief Type of the callback being called when a single block of data is
 *        received as part of CoAP observe notification.
 */
typedef int (*golioth_blockwise_observe_received_t)(struct golioth_blockwise_observe_ctx *ctx,
						    const uint8_t *data,
						    size_t offset, size_t len,
						    bool last);

/**
 * @brief Represents a Golioth blockwise observe context.
 */
struct golioth_blockwise_observe_ctx {
	struct coap_block_context block_ctx;
	struct golioth_client *client;
	const char *path;
	golioth_blockwise_observe_received_t received_cb;
};

static inline void golioth_lock(struct golioth_client *client)
{
	k_mutex_lock(&client->lock, K_FOREVER);
}

static inline void golioth_unlock(struct golioth_client *client)
{
	k_mutex_unlock(&client->lock);
}

/**
 * @brief Initialize golioth client instance
 *
 * Initializes internal data of client instance. Must be called before using any
 * other APIs on client instance.
 *
 * @param client Client instance
 */
void golioth_init(struct golioth_client *client);

/**
 * @brief Connect to Golioth
 *
 * Attempt to connect to Golioth.
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_connect(struct golioth_client *client);

/**
 * @brief Disconnect from Golioth
 *
 * Attempt to disconnect from Golioth.
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_disconnect(struct golioth_client *client);

/**
 * @brief Set UDP as transport protocol
 *
 * Set UDP as transport protocol for CoAP packets to Golioth and assignes
 * credentials (identity) to be used.
 *
 * @param client Client instance
 * @param identity Client identity
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_set_proto_coap_udp(struct golioth_client *client,
			       uint8_t *identity, size_t identity_len);

/**
 * @brief Set DTLS as transport protocol
 *
 * Set DTLS as transport protocol for CoAP packets to Golioth and assignes
 * credentials to be used.
 *
 * @param client Client instance
 * @param sec_tag_list Secure tag array (see @ref sec_tag_t and
 *                      @ref TLS_SEC_TAG_LIST)
 * @param sec_tag_count Secure tag count (see @ref sec_tag_t and
 *                      @ref TLS_SEC_TAG_LIST)
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_set_proto_coap_dtls(struct golioth_client *client,
				sec_tag_t *sec_tag_list,
				size_t sec_tag_count);

/**
 * @brief Send CoAP packet to Golioth
 *
 * This is low-level API for sending arbitrary CoAP packet to Golioth.
 *
 * @param client Client instance
 * @param packet CoAP packet
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_send_coap(struct golioth_client *client,
		      struct coap_packet *packet);

/**
 * @brief Send CoAP packet with separate payload to Golioth
 *
 * Similar to @ref golioth_send_coap, but appends payload (internally) before
 * sending.
 *
 * @param client Client instance
 * @param packet CoAP packet (without payload)
 * @param data Payload data
 * @param data Payload length
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_send_coap_payload(struct golioth_client *client,
			      struct coap_packet *packet,
			      uint8_t *data, uint16_t data_len);

/**
 * @brief Send Hello message to Golioth
 *
 * Sends Hello message to Golioth, which is mostly useful verifying Golioth
 * connection.
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_send_hello(struct golioth_client *client);

/**
 * @brief Get value from Golioth's Light DB
 *
 * Get value from Light DB and initialize passed CoAP reply handler.
 *
 * @param client Client instance
 * @param path Light DB resource path
 * @param format Requested format of payload
 * @param reply CoAP reply handler object used for notifying about received
 *              value
 * @param reply_cb Reply handler callback
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_get(struct golioth_client *client, const uint8_t *path,
			enum coap_content_format format,
			struct coap_reply *reply, coap_reply_t reply_cb);

/**
 * @brief Set value to Golioth's Light DB
 *
 * Set new value to Light DB.
 *
 * @param client Client instance
 * @param path Light DB resource path
 * @param format Format of payload
 * @param data Payload data
 * @param data_len Payload length
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum coap_content_format format,
			uint8_t *data, uint16_t data_len);

/**
 * @brief Observe value in Golioth's Light DB
 *
 * Observe value in Light DB and initialize passed CoAP reply handler.
 *
 * @param client Client instance
 * @param path Light DB resource path to be monitored
 * @param format Requested format of payload
 * @param reply CoAP reply handler object used for notifying about updated
 *              value
 * @param reply_cb Reply handler callback
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_observe(struct golioth_client *client, const uint8_t *path,
			    enum coap_content_format format,
			    struct coap_reply *reply, coap_reply_t reply_cb);

/**
 * @brief Observe resource with blockwise updates
 *
 * @param client Client instance
 * @param ctx Blockwise observe context that will be used for handling resouce
 *            updates
 * @param path Resource path to be monitored
 * @param reply CoAP reply handler object used for notifying about updated
 *              value
 * @param received_cb Received block handler callback
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_observe_blockwise(struct golioth_client *client,
			      struct golioth_blockwise_observe_ctx *ctx,
			      const char *path, struct coap_reply *reply,
			      golioth_blockwise_observe_received_t received_cb);

/**
 * @brief Process incoming data from Golioth
 *
 * Process incoming data on network socket. It does not block when there is no
 * more data, so it is best to use it with @ref zsock_poll.
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_process_rx(struct golioth_client *client);

#endif /* ZEPHYR_INCLUDE_NET_GOLIOTH_H_ */
