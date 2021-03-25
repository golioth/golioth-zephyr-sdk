/*
 * Copyright (c) 2021 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_NET_GOLIOTH_H_
#define ZEPHYR_INCLUDE_NET_GOLIOTH_H_

#include <kernel.h>
#include <net/coap.h>
#include <net/tls_credentials.h>
#include <stdint.h>

#define GOLIOTH_MAX_IDENTITY_LEN	32
#define GOLIOTH_EMPTY_PACKET_LEN	(16 + GOLIOTH_MAX_IDENTITY_LEN)

struct golioth_unsecure {
	const uint8_t *identity;
	size_t identity_len;
};

struct golioth_tls {
	sec_tag_t *sec_tag_list;
	size_t sec_tag_count;
};

struct golioth_client {
	int proto;
	const struct sockaddr *server;

	union {
		struct golioth_unsecure unsecure;
		struct golioth_tls tls;
	};

	uint8_t *rx_buffer;
	size_t rx_buffer_len;

	struct coap_packet rx_packet;
	struct coap_option rx_options[CONFIG_NET_GOLIOTH_COAP_MAX_OPTIONS];

	struct k_mutex lock;
	int sock;

	void (*on_message)(struct golioth_client *client,
			   struct coap_packet *rx);
};

static inline void golioth_lock(struct golioth_client *client)
{
	k_mutex_lock(&client->lock, K_FOREVER);
}

static inline void golioth_unlock(struct golioth_client *client)
{
	k_mutex_unlock(&client->lock);
}

void golioth_init(struct golioth_client *client);
int golioth_connect(struct golioth_client *client);
int golioth_disconnect(struct golioth_client *client);

int golioth_set_proto_coap_udp(struct golioth_client *client,
			       uint8_t *identity, size_t identity_len);
int golioth_set_proto_coap_dtls(struct golioth_client *client,
				sec_tag_t *sec_tag_list,
				size_t sec_tag_count);

int golioth_send_coap(struct golioth_client *client,
		      struct coap_packet *packet);
int golioth_send_coap_payload(struct golioth_client *client,
			      struct coap_packet *packet,
			      uint8_t *data, uint16_t data_len);
int golioth_send_hello(struct golioth_client *client);

int golioth_process_rx(struct golioth_client *client);

#endif /* ZEPHYR_INCLUDE_NET_GOLIOTH_H_ */
