/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_LIGHTDB_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_LIGHTDB_H_

#include <stdint.h>
#include <zephyr/net/coap.h>
#include <net/golioth/req.h>

/**
 * @defgroup golioth_lightdb Golioth LightDB
 * @ingroup net
 * Functions for interacting with the Golioth LightDB service
 * @{
 */

struct golioth_client;
enum golioth_content_format;

#define GOLIOTH_LIGHTDB_PATH(x)		".d/" x
#define GOLIOTH_LIGHTDB_STREAM_PATH(x)	".s/" x

/**
 * @brief Get value from Golioth's LightDB (callback based)
 *
 * Asynchronously request value from Golioth's LightDB and let @p cb be invoked when such value is
 * retrieved or some error condtition happens.
 *
 * @warning Experimental API
 *
 * @param[in] client Client instance
 * @param[in] path LightDB resource path
 * @param[in] format Requested format of payload
 * @param[in] cb Callback executed on response received, timeout or error
 * @param[in] user_data User data passed to @p cb
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_get_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   golioth_req_cb_t cb, void *user_data);

/**
 * @brief Get value from Golioth's LightDB (synchronous, into preallocated buffer)
 *
 * Synchronously get value from Golioth's LightDB and store it into preallocated buffer.
 *
 * @param[in] client Client instance
 * @param[in] path LightDB resource path
 * @param[in] format Requested format of payload
 * @param[in] data Buffer for received data
 * @param[in,out] len Size of buffer on input, size of response on output
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_get(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			uint8_t *data, size_t *len);

/**
 * @brief Set value to Golioth's LightDB
 *
 * Set new value to LightDB.
 *
 * @param client Client instance
 * @param path LightDB resource path
 * @param format Format of payload
 * @param data Payload data
 * @param data_len Payload length
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			uint8_t *data, uint16_t data_len);

/**
 * @brief Delete value in Golioth's LightDB
 *
 * Delete value in LightDB.
 *
 * @param client Client instance
 * @param path LightDB resource path
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_delete(struct golioth_client *client, const uint8_t *path);

/**
 * @brief Observe value in Golioth's LightDB
 *
 * Observe value in LightDB and initialize passed CoAP reply handler.
 *
 * @param client Client instance
 * @param path LightDB resource path to be monitored
 * @param format Requested format of payload
 * @param reply CoAP reply handler object used for notifying about updated
 *              value
 * @param reply_cb Reply handler callback
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_observe(struct golioth_client *client, const uint8_t *path,
			    enum golioth_content_format format,
			    struct coap_reply *reply, coap_reply_t reply_cb);

/** @} */

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_LIGHTDB_H_ */
