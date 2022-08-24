/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_

#include <stdint.h>
#include <zephyr/net/coap.h>

struct _QCBOREncodeContext;
struct _QCBOREncodeContext;

typedef struct _QCBORDecodeContext QCBORDecodeContext;
typedef struct _QCBOREncodeContext QCBOREncodeContext;

/**
 * @defgroup golioth_rpc Golioth Remote Procedure Call
 * @ingroup net
 * Functions for interacting with the Golioth Remote Procedure Call service
 * @{
 */

struct golioth_client;

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
 * using QCBOR functions like QCBORDecode_GetDouble, QCBORDecode_GetTextString, etc.
 *
 * If the RPC needs to return data, it can be added to response_detail_map
 * using QCBOR functions like QCBOREncode_AddDoubleToMap, QCBOREncode_AddTextToMap, etc.
 *
 * Here is an example of a callback function that implements the "on_multiply"
 * method, which multiplies two input numbers and returns the result.
 *
 * @code{.c}
 * static enum golioth_rpc_status on_multiply(QCBORDecodeContext* request_params_array,
 *                                            QCBOREncodeContext* response_detail_map,
 *                                            void *callback_arg)
 * {
 *      double a, b;
 *      QCBORDecode_GetDouble(request_params_array, &a);
 *      QCBORDecode_GetDouble(request_params_array, &b);
 *      QCBORError qerr = QCBORDecode_GetError(request_params_array);
 *      if (qerr != QCBOR_SUCCESS) {
 *            LOG_ERR("Failed to decode array items: %d (%s)", qerr, qcbor_err_to_str(qerr));
 *            return GOLIOTH_RPC_INVALID_ARGUMENT;
 *      }
 *
 *      double value = a * b;
 *      QCBOREncode_AddDoubleToMap(response_detail_map, "value", value);
 *      return GOLIOTH_RPC_OK;
 * }
 * @endcode
 *
 * @param request_params_array QCBOR decode context, inside of the RPC request params array
 * @param response_detail_map QCBOR encode context, inside of the RPC response detail map
 * @param callback_arg callback_arg, unchanged from callback_arg of @ref golioth_rpc_register
 *
 * @return GOLIOTH_RPC_OK - if method was called successfully
 * @return GOLIOTH_RPC_INVALID_ARGUMENT - if params were invalid
 * @return otherwise - method failure
 */
typedef enum golioth_rpc_status (*golioth_rpc_cb_fn)(QCBORDecodeContext *request_params_array,
						     QCBOREncodeContext *response_detail_map,
						     void *callback_arg);

/**
 * @brief Data for each registered RPC method
 */
struct golioth_rpc_method {
	const char *name;
	golioth_rpc_cb_fn callback;
	void *callback_arg;
};

/**
 * @brief Global/shared RPC state data, placed in struct golioth_client
 */
struct golioth_rpc {
#if defined(CONFIG_GOLIOTH_RPC)
	bool initialized;
	struct coap_reply observe_reply;
	struct golioth_rpc_method methods[CONFIG_GOLIOTH_RPC_MAX_NUM_METHODS];
	int num_methods;
#endif
};

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

/** @} */

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_RPC_H_ */
