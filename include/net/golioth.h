/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_H_

#include <net/golioth/lightdb.h>
#include <net/golioth/rpc.h>
#include <net/golioth/settings.h>
#include <net/golioth/stream.h>
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

/**
 * @brief Set of Content-Format option values for Golioth APIs
 */
enum golioth_content_format {
	GOLIOTH_CONTENT_FORMAT_APP_OCTET_STREAM = COAP_CONTENT_FORMAT_APP_OCTET_STREAM,
	GOLIOTH_CONTENT_FORMAT_APP_JSON = COAP_CONTENT_FORMAT_APP_JSON,
	GOLIOTH_CONTENT_FORMAT_APP_CBOR = COAP_CONTENT_FORMAT_APP_CBOR,
};

#ifdef __cplusplus
extern "C" {
#endif

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

	struct golioth_tls tls;

	uint8_t *rx_buffer;
	size_t rx_buffer_len;
	size_t rx_received;

	struct coap_packet rx_packet;

	struct k_mutex lock;
	int sock;

	sys_dlist_t coap_reqs;
	bool coap_reqs_connected;
	struct k_mutex coap_reqs_lock;

	void (*on_connect)(struct golioth_client *client);

	void (*wakeup)(struct golioth_client *client);

	struct golioth_rpc rpc;
	struct golioth_settings settings;
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
 * @param sec_tag_list Secure tag array (see sec_tag_t and
 *                      TLS_SEC_TAG_LIST)
 * @param sec_tag_count Secure tag count (see sec_tag_t and
 *                      TLS_SEC_TAG_LIST)
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
 * @param data_len Payload length
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
 * @brief Process incoming data from Golioth
 *
 * Process incoming data on network socket. It does not block when there is no
 * more data, so it is best to use it with zsock_poll.
 *
 * @param client Client instance
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_process_rx(struct golioth_client *client);

/**
 * @brief Prepare for poll() system call on transport socket
 *
 * @param[in] client Client instance
 * @param[in] now Timestamp in msec for current event loop (e.g. output of k_uptime_get())
 * @param[out] fd File descriptor of transport socket (optional, can be NULL)
 * @param[out] timeout Timeout till the next action needs to be taken, such as resending a packet
 *                     (optional, can be NULL)
 */
void golioth_poll_prepare(struct golioth_client *client, int64_t now,
			  int *fd, int64_t *timeout);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_H_ */
