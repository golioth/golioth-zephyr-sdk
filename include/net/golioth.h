/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_H_

#include <net/golioth/rpc.h>
#include <net/golioth/settings.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/tls_credentials.h>

/**
 * @defgroup net Golioth Networking
 * Functions for communicating with the Golioth servers
 * @{
 */

#define GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN	128

#define GOLIOTH_MAX_IDENTITY_LEN	32
#define GOLIOTH_EMPTY_PACKET_LEN	(16 + GOLIOTH_MAX_IDENTITY_LEN)

#define GOLIOTH_MAX_NUM_MESSAGE_CALLBACKS 4

#define GOLIOTH_LIGHTDB_PATH(x)		".d/" x
#define GOLIOTH_LIGHTDB_STREAM_PATH(x)	".s/" x

/**
 * @brief (D)TLS credentials of Golioth client.
 */
struct golioth_tls {
	sec_tag_t *sec_tag_list;
	size_t sec_tag_count;
};

/**
 * @brief Callback function type to handle received CoAP packets.
 */
struct golioth_client;
typedef void (*golioth_message_callback)(struct golioth_client *client,
					 struct coap_packet *rx,
					 void *user_arg);

/**
 * @brief Data associated with a message callback registration.
 */
struct golioth_message_callback_reg {
	golioth_message_callback callback;
	void *user_arg;
};

/**
 * @brief Represents a Golioth client instance.
 */
struct golioth_client {
	int proto;

	struct golioth_tls tls;

	uint8_t *rx_buffer;
	size_t rx_buffer_len;
	size_t rx_received;

	struct coap_packet rx_packet;
	struct coap_option rx_options[CONFIG_GOLIOTH_COAP_MAX_OPTIONS];

	struct k_mutex lock;
	int sock;

	void (*on_connect)(struct golioth_client *client);
	void (*on_message)(struct golioth_client *client, struct coap_packet *rx);

	/* Storage for additional on_message callbacks */
	struct golioth_message_callback_reg message_callbacks[GOLIOTH_MAX_NUM_MESSAGE_CALLBACKS];
	size_t num_message_callbacks;

	struct golioth_rpc rpc;
	struct k_mutex rpc_mutex;

	struct golioth_settings settings;
};

struct golioth_blockwise_download_ctx;

/**
 * @typedef golioth_blockwise_download_received_t
 * @brief Type of the callback being called when a single block of data is
 *        received as part of CoAP response.
 */
typedef int (*golioth_blockwise_download_received_t)(struct golioth_blockwise_download_ctx *ctx,
						     const uint8_t *data,
						     size_t offset, size_t len,
						     bool last);

/**
 * @brief Represents blockwise download transfer from Golioth.
 */
struct golioth_blockwise_download_ctx {
	struct coap_block_context block_ctx;
	struct golioth_client *client;
	struct coap_reply *reply;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	golioth_blockwise_download_received_t received_cb;
};

/**
 * @brief Initialize blockwise download
 *
 * @param client Client instance
 * @param ctx Blockwise download context
 */
void golioth_blockwise_download_init(struct golioth_client *client,
				     struct golioth_blockwise_download_ctx *ctx);

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
 * @brief Check if client is connected to Golioth
 *
 * Check if client instance is connected to Golioth.
 *
 * @param client Client instance.
 *
 * @retval true When client is connected to Golioth.
 * @retval false When client is not connected to Golioth.
 */
bool golioth_is_connected(struct golioth_client *client);

/**
 * @brief Connect to Golioth
 *
 * Attempt to connect to Golioth.
 *
 * @param client Client instance
 * @param host Server hostname or IP address
 * @param port Server port number
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_connect(struct golioth_client *client, const char *host,
		    uint16_t port);

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
 * @brief Send PING message to Golioth
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_ping(struct golioth_client *client);

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
 * @brief Delete value in Golioth's Light DB
 *
 * Delete value in Light DB.
 *
 * @param client Client instance
 * @param path Light DB resource path
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_delete(struct golioth_client *client, const uint8_t *path);

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

/**
 * @brief Register a callback to be called when a CoAP message is received.
 *
 * This is similar to client->on_message, but allows for more than one "on_message"
 * callback to be registered, and has an additional user_arg parameter which
 * can be used to pass user data to the callback when it's invoked.
 *
 * @param client Client instance
 * @param callback Message callback to register
 * @param user_arg User data forwarded directly to callback when invoked. Optional, can be NULL.
 *
 * @retval 0 registration successful
 * @retval <0 registration failed
 */
int golioth_register_message_callback(struct golioth_client *client,
				      golioth_message_callback callback,
				      void *user_arg);

/** @} */

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_H_ */
