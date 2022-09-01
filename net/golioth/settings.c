/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/settings.h>
#include <net/golioth/system_client.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <stdio.h>
#include <assert.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"

/*
 * Example settings request from cloud:
 *
 * {
 *   "version": 1652109801583 // Unix timestamp with the most recent change
 *   "settings": {
 *     "MOTOR_SPEED": 100,
 *     "UPDATE_INTERVAL": 100,
 *     "TEMPERATURE_FORMAT": "celsius"
 *   }
 * }
 *
 * Example settings response from device:
 *
 * {
 *   "version": 1652109801583 // timestamp, copied from request
 *   "errors": [ // if no errors, then omit
 *      { "setting_key": "string", "error_code": integer, "details": "string" },
 *      ...
 *   ]
 * }
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_SETTINGS_PATH ".c"
#define GOLIOTH_SETTINGS_STATUS_PATH ".c/status"
#define GOLIOTH_SETTINGS_MAX_NAME_LEN 63 /* not including NULL */
#define GOLIOTH_SETTINGS_MAX_RESPONSE_LEN 256

struct settings_response {
	QCBOREncodeContext encode_ctx;
	UsefulBuf useful_buf;
	uint8_t buf[GOLIOTH_SETTINGS_MAX_RESPONSE_LEN];
	size_t num_errors;
};

static int send_coap_response(struct golioth_client *client,
			      uint8_t *coap_payload,
			      size_t coap_payload_len)
{
	return golioth_coap_req_cb(client, COAP_METHOD_POST, PATHV(GOLIOTH_SETTINGS_STATUS_PATH),
				   COAP_CONTENT_FORMAT_APP_CBOR,
				   coap_payload, coap_payload_len,
				   NULL, NULL,
				   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

static void init_response(struct settings_response *response, int64_t version)
{
	memset(response, 0, sizeof(*response));
	response->useful_buf = (UsefulBuf){ response->buf, sizeof(response->buf) };
	QCBOREncode_Init(&response->encode_ctx, response->useful_buf);

	/* Initialize the map and add the "version" */
	QCBOREncode_OpenMap(&response->encode_ctx);
	QCBOREncode_AddInt64ToMap(&response->encode_ctx, "version", version);
}

static void add_error_to_response(struct settings_response *response,
				  const char *key,
				  enum golioth_settings_status code)
{
	if (response->num_errors == 0) {
		QCBOREncode_OpenArrayInMap(&response->encode_ctx, "errors");
	}

	QCBOREncode_OpenMap(&response->encode_ctx);
	QCBOREncode_AddSZStringToMap(&response->encode_ctx, "setting_key", key);
	QCBOREncode_AddInt64ToMap(&response->encode_ctx, "error_code", code);
	QCBOREncode_CloseMap(&response->encode_ctx);

	response->num_errors++;
}

static int finalize_and_send_response(struct golioth_client *client,
				      struct settings_response *response)
{
	/*
	 * If there were errors, then the "errors" array is still open,
	 * so we need to close it.
	 */
	if (response->num_errors > 0) {
		QCBOREncode_CloseArray(&response->encode_ctx);
	}

	/* Close the root map */
	QCBOREncode_CloseMap(&response->encode_ctx);

	size_t response_len;
	QCBORError qerr = QCBOREncode_FinishGetSize(&response->encode_ctx, &response_len);

	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("QCBOREncode_FinishGetSize error: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return qcbor_error_to_posix(qerr);
	}

	LOG_HEXDUMP_DBG(response->buf, response_len, "Response");

	return send_coap_response(client, response->buf, response_len);
}

static int on_setting(struct golioth_req_rsp *rsp)
{
	struct golioth_client *client = rsp->user_data;

	if (rsp->err) {
		LOG_ERR("Error on Settings observation: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, "Payload");

	if (rsp->len == 3 && rsp->data[1] == 'O' && rsp->data[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	UsefulBufC payload_buf = { rsp->data, rsp->len };

	QCBORDecodeContext decode_ctx;
	QCBORItem decoded_item;
	QCBORError qerr;
	int64_t version;

	QCBORDecode_Init(&decode_ctx, payload_buf, QCBOR_DECODE_MODE_NORMAL);
	QCBORDecode_EnterMap(&decode_ctx, NULL);
	QCBORDecode_GetInt64InMapSZ(&decode_ctx, "version", &version);
	qerr = QCBORDecode_GetError(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to parse settings version: %d (%s)", qerr, qcbor_err_to_str(qerr));
		QCBORDecode_ExitMap(&decode_ctx);
		QCBORDecode_Finish(&decode_ctx);
		return qcbor_error_to_posix(qerr);
	}

	QCBORDecode_EnterMapFromMapSZ(&decode_ctx, "settings");

	struct settings_response settings_response;

	init_response(&settings_response, version);

	/*
	 * We don't know how many items are in the settings map, so we will
	 * iterate until we get an error from QCBOR
	 */
	qerr = QCBORDecode_GetNext(&decode_ctx, &decoded_item);

	bool has_more_settings = (qerr == QCBOR_SUCCESS);

	while (has_more_settings) {
		/* Handle item */
		uint8_t data_type = decoded_item.uDataType;
		UsefulBufC label = decoded_item.label.string;
		char key[GOLIOTH_SETTINGS_MAX_NAME_LEN + 1] = {};

		/* Copy setting label/name and ensure it's NULL-terminated */
		assert((decoded_item.uLabelType == QCBOR_TYPE_BYTE_STRING) ||
		       (decoded_item.uLabelType == QCBOR_TYPE_TEXT_STRING));
		memcpy(key,
		       label.ptr,
		       MIN(GOLIOTH_SETTINGS_MAX_NAME_LEN, label.len));

		LOG_DBG("key = %s, type = %d", key, data_type);

		bool data_type_valid = true;
		struct golioth_settings_value value = {};

		if (data_type == QCBOR_TYPE_INT64) {
			value.type = GOLIOTH_SETTINGS_VALUE_TYPE_INT64;
			value.i64 = decoded_item.val.int64;
		} else if (data_type == QCBOR_TYPE_DOUBLE) {
			value.type = GOLIOTH_SETTINGS_VALUE_TYPE_FLOAT;
			value.f = (float)decoded_item.val.dfnum;
		} else if (data_type == QCBOR_TYPE_TRUE) {
			value.type = GOLIOTH_SETTINGS_VALUE_TYPE_BOOL;
			value.b = true;
		} else if (data_type == QCBOR_TYPE_FALSE) {
			value.type = GOLIOTH_SETTINGS_VALUE_TYPE_BOOL;
			value.b = false;
		} else if (data_type == QCBOR_TYPE_TEXT_STRING) {
			value.type = GOLIOTH_SETTINGS_VALUE_TYPE_STRING;
			value.string.ptr = (const char *)decoded_item.val.string.ptr;
			value.string.len = decoded_item.val.string.len;
		} else {
			LOG_WRN("Unrecognized data type: %d", data_type);
			data_type_valid = false;
			add_error_to_response(&settings_response,
					      key,
					      GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID);
		}

		if (data_type_valid) {
			enum golioth_settings_status setting_status =
				client->settings.callback(key, &value);

			if (setting_status != GOLIOTH_SETTINGS_SUCCESS) {
				add_error_to_response(&settings_response, key, setting_status);
			}
		}

		/* Get next item */
		qerr = QCBORDecode_GetNext(&decode_ctx, &decoded_item);
		has_more_settings = (qerr == QCBOR_SUCCESS);
	}

	QCBORDecode_ExitMap(&decode_ctx); /* settings */
	QCBORDecode_ExitMap(&decode_ctx); /* root */
	QCBORDecode_Finish(&decode_ctx);

	return finalize_and_send_response(client, &settings_response);
}

static int golioth_settings_observe(struct golioth_client *client)
{
	return golioth_coap_req_cb(client, COAP_METHOD_GET, PATHV(GOLIOTH_SETTINGS_PATH),
				   COAP_CONTENT_FORMAT_APP_CBOR,
				   NULL, 0,
				   on_setting, client,
				   GOLIOTH_COAP_REQ_OBSERVE);
}

int golioth_settings_register_callback(struct golioth_client *client,
				       golioth_settings_cb callback)
{
	if (!callback) {
		LOG_ERR("Callback must not be NULL");
		return -EINVAL;
	}

	/*
	 * One-time registration of callback.
	 *
	 * It's possible this function gets called again on a client reconnect,
	 * in which case we need to make sure not to re-register the callback.
	 */
	if (!client->settings.initialized) {
		client->settings.callback = callback;
		client->settings.initialized = true;
	}

	return golioth_settings_observe(client);
}
