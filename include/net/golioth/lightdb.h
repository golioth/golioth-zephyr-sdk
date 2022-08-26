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
 * @brief Set value to Golioth's LightDB (callback based)
 *
 * Asynchronously request to store new value to LightDB and let @p cb be invoked when such value is
 * actually stored or some error condition happens.
 *
 * @warning Experimental API
 *
 * @param[in] client Client instance
 * @param[in] path LightDB resource path
 * @param[in] format Requested format of payload
 * @param[in] data Pointer to data to be set
 * @param[in] data_len Length of data to be set
 * @param[in] cb Callback executed on response received, timeout or error
 * @param[in] user_data User data passed to @p cb
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_set_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   const uint8_t *data, size_t data_len,
			   golioth_req_cb_t cb, void *user_data);

/**
 * @brief Set value to Golioth's LightDB (synchronously)
 *
 * Synchronously set new value to LightDB.
 *
 * @param[in] client Client instance
 * @param[in] path LightDB resource path
 * @param[in] format Format of payload
 * @param[in] data Payload data
 * @param[in] data_len Payload length
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_set(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			const uint8_t *data, size_t data_len);

/**
 * @brief Observe value in Golioth's LightDB (callback based)
 *
 * Asynchronously request to observe value in Golioth's LightDB and let @p cb be invoked when such
 * value is retrieved (for the first time or after an update) or some error condition happens.
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
int golioth_lightdb_observe_cb(struct golioth_client *client, const uint8_t *path,
			       enum golioth_content_format format,
			       golioth_req_cb_t cb, void *user_data);

/**
 * @brief Delete value in Golioth's LightDB
 *
 * Delete value in LightDB.
 *
 * @param[in] client Client instance
 * @param[in] path LightDB resource path
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_lightdb_delete(struct golioth_client *client, const uint8_t *path);

/** @} */

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_LIGHTDB_H_ */
