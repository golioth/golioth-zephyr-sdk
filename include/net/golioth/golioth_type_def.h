#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_TYPE_DEF_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_TYPE_DEF_H_

#include <zephyr/kernel.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/coap.h>

/**
 * @brief (D)TLS credentials of Golioth client.
 */
struct golioth_tls {
	sec_tag_t *sec_tag_list;
	size_t sec_tag_count;
};

/**
 * @brief Set of Content-Format option values for Golioth APIs
 */
enum golioth_content_format {
	GOLIOTH_CONTENT_FORMAT_APP_OCTET_STREAM = COAP_CONTENT_FORMAT_APP_OCTET_STREAM,
	GOLIOTH_CONTENT_FORMAT_APP_JSON = COAP_CONTENT_FORMAT_APP_JSON,
	GOLIOTH_CONTENT_FORMAT_APP_CBOR = COAP_CONTENT_FORMAT_APP_CBOR,
};

/**
 * @brief Global/shared RPC state data, placed in struct golioth_client
 */
struct golioth_rpc {
#if defined(CONFIG_GOLIOTH_RPC)
	struct golioth_rpc_method methods[CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS];
	int num_methods;
	struct k_mutex mutex;
#endif
};

/**
 * @brief Settings state data, placed in struct golioth_client
 */
struct golioth_settings {
#if defined(CONFIG_GOLIOTH_SETTINGS)
	bool initialized;
	golioth_settings_cb callback;
#endif
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

#endif