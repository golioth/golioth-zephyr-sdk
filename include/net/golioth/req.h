/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_REQ_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_REQ_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @typedef golioth_get_next_cb_t
 *
 * @brief Callback function for requesting more data to be received.
 */
typedef int (*golioth_get_next_cb_t)(void *get_next_data, int status);

/**
 * @brief Information about response to user request
 *
 * Stores information about receive response, acknowledgment, error condition (e.g. timeout, error
 * received from server).
 *
 * If @p err is <0, then request failed with error and just @p user_data stores valid information.
 * If @p err is 0, then response was successfully received (either with data, or just an
 * acknowledgment).
 */
struct golioth_req_rsp {
	const uint8_t *data;
	size_t len;
	size_t off;

	size_t total;

	/* TODO: provide more user-friendly helper function */
	golioth_get_next_cb_t get_next;
	void *get_next_data;

	void *user_data;

	int err;
};

/**
 * @typedef golioth_req_cb_t
 *
 * @brief User callback for handling valid response from server or error condition
 */
typedef int (*golioth_req_cb_t)(struct golioth_req_rsp *rsp);

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_REQ_H_ */
