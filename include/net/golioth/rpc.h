/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_

#include <stdint.h>
#include <zephyr/net/coap.h>
#include <zephyr/kernel.h>
#include <net/golioth/golioth_type_def.h>

#ifdef CONFIG_ZCBOR
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#else
typedef struct {} zcbor_state_t;
#endif

/**
 * @defgroup golioth_rpc Golioth Remote Procedure Call
 * @ingroup net
 * Functions for interacting with the Golioth Remote Procedure Call service
 * @{
 */

// struct golioth_client;

/**
 * @brief Enumeration of RPC status codes, sent in the RPC response.
 */
enum golioth_rpc_status {
	GOLIOTH_RPC_OK = 0,
	GOLIOTH_RPC_CANCELED = 1,
	GOLIOTH_RPC_UNKNOWN = 2,
	GOLIOTH_RPC_INVALID_ARGUMENT = 3,
	GOLIOTH_RPC_DEADLINE_EXCEEDED = 4,
	GOLIOTH_RPC_NOT_FOUND = 5,
	GOLIOTH_RPC_ALREADYEXISTS = 6,
	GOLIOTH_RPC_PERMISSION_DENIED = 7,
	GOLIOTH_RPC_RESOURCE_EXHAUSTED = 8,
	GOLIOTH_RPC_FAILED_PRECONDITION = 9,
	GOLIOTH_RPC_ABORTED = 10,
	GOLIOTH_RPC_OUT_OF_RANGE = 11,
	GOLIOTH_RPC_UNIMPLEMENTED = 12,
	GOLIOTH_RPC_INTERNAL = 13,
	GOLIOTH_RPC_UNAVAILABLE = 14,
	GOLIOTH_RPC_DATA_LOSS = 15,
	GOLIOTH_RPC_UNAUTHENTICATED = 16,
};

/**
 * @brief Callback function type for remote procedure call
 *
 * If the RPC has input params, they can be extracted from request_params_array
 * using zcbor functions like zcbor_float_decode, zcbor_tstr_decode, etc.
 *
 * If the RPC needs to return data, it can be added to response_detail_map
 * using zcbor functions like zcbor_tstr_put_lit, zcbor_float64_put, etc.
 *
 * Here is an example of a callback function that implements the "on_multiply"
 * method, which multiplies two input numbers and returns the result.
 *
 * @code{.c}
 * static enum golioth_rpc_status on_multiply(zcbor_state_t *request_params_array,
 *                                            zcbor_state_t *response_detail_map,
 *                                            void *callback_arg)
 * {
 *      double a, b;
 *      double value;
 *      bool ok;
 *
 *      ok = zcbor_float_decode(request_params_array, &a) &&
 *           zcbor_float_decode(request_params_array, &b);
 *      if (!ok) {
 *            LOG_ERR("Failed to decode array items");
 *            return GOLIOTH_RPC_INVALID_ARGUMENT;
 *      }
 *
 *      value = a * b;
 *
 *      ok = zcbor_tstr_put_lit(response_detail_map, "value") &&
 *           zcbor_float64_put(response_detail_map, value);
 *      if (!ok) {
 *            LOG_ERR("Failed to encode value");
 *            return GOLIOTH_RPC_RESOURCE_EXHAUSTED;
 *      }
 *
 *      return GOLIOTH_RPC_OK;
 * }
 * @endcode
 *
 * @param request_params_array zcbor decode state, inside of the RPC request params array
 * @param response_detail_map zcbor encode state, inside of the RPC response detail map
 * @param callback_arg callback_arg, unchanged from callback_arg of @ref golioth_rpc_register
 *
 * @return GOLIOTH_RPC_OK - if method was called successfully
 * @return GOLIOTH_RPC_INVALID_ARGUMENT - if params were invalid
 * @return otherwise - method failure
 */
typedef enum golioth_rpc_status (*golioth_rpc_cb_fn)(zcbor_state_t *request_params_array,
						     zcbor_state_t *response_detail_map,
						     void *callback_arg);

/**
 * @brief Data for each registered RPC method
 */
struct golioth_rpc_method {
	const char *name;
	golioth_rpc_cb_fn callback;
	void *callback_arg;
};

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize RPC
 *
 * @param client Client instance
 *
 * @return 0 - RPC initialized successfully
 * @return Otherwise - RPC error initializing
 */
int golioth_rpc_init(struct golioth_client *client);

/**
 * @brief Register an RPC method
 *
 * @param client Client instance
 * @param method_name The name of the method to register
 * @param callback The callback to be invoked, when an RPC request with matching method name
 *         is received by the client.
 * @param callback_arg User data forwarded to callback when invoked. Optional, can be NULL.
 *
 * @return 0 - RPC method successfully registered
 * @return <0 - Error registering RPC method
 */
int golioth_rpc_register(struct golioth_client *client,
			 const char *method_name,
			 golioth_rpc_cb_fn callback,
			 void *callback_arg);

/**
 * @brief Observe for RPC method invocations
 *
 * User applications should call this function in the `on_connect` callback,
 * if they wish to observe RPCs.
 *
 * Establishes a single observation for endpoint ".rpc".
 * The handler for this endpoint will look up the method in a table of
 * registered RPCs (from @ref golioth_rpc_register) and invoke the callback
 * if the method is found.
 *
 * @param client Client instance
 *
 * @return 0 - RPC observation established
 * @return <0 - Error observing RPC
 */
int golioth_rpc_observe(struct golioth_client *client);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_ */
