/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_STREAM_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_STREAM_H_

#include <stdint.h>
#include <zephyr/net/coap.h>
#include <net/golioth/req.h>
#include <net/golioth/golioth_type_def.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup golioth_stream Golioth LightDB Stream
 * @ingroup net
 * Functions for interacting with the Golioth LightDB Stream service
 * @{
 */

/**
 * @brief Push value to Golioth's LightDB Stream (callback based)
 *
 * Asynchronously push new value to LightDB Stream and let @p cb be invoked when server
 * acknowledges it or some error condition happens.
 *
 * @warning Experimental API
 *
 * @param[in] client Client instance
 * @param[in] path LightDB Stream resource path
 * @param[in] format Format of payload
 * @param[in] data Payload data
 * @param[in] data_len Payload length
 * @param[in] cb Callback executed on response received, timeout or error
 * @param[in] user_data User data passed to @p cb
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_stream_push_cb(struct golioth_client *client, const uint8_t *path,
			   enum golioth_content_format format,
			   const uint8_t *data, size_t data_len,
			   golioth_req_cb_t cb, void *user_data);

/**
 * @brief Push value to Golioth's LightDB Stream (synchronously)
 *
 * Synchronously push new value to LightDB Stream.
 *
 * @param[in] client Client instance
 * @param[in] path LightDB Stream resource path
 * @param[in] format Format of payload
 * @param[in] data Payload data
 * @param[in] data_len Payload length
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_stream_push(struct golioth_client *client, const uint8_t *path,
			enum golioth_content_format format,
			const uint8_t *data, size_t data_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_STREAM_H_ */
