/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/settings.h>
#include <net/golioth/system_client.h>
#include <logging/log.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <settings/settings.h>
#include <stdio.h>
#include <assert.h>

#include "coap_utils.h"

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
 *   "error_code": 0 // Define error codes
 *   "version": 1652109801583 // Report 0 for errors or ignored for errors
 * }
 */

LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_SETTINGS_PATH ".c"
#define GOLIOTH_SETTINGS_STATUS_PATH ".c/status"
#define GOLIOTH_SETTINGS_MAX_NAME_LEN 63
#define GOLIOTH_SETTINGS_MAX_RESPONSE_LEN 63

static int send_coap_response(struct golioth_client *client,
			      uint8_t *coap_payload,
			      size_t coap_payload_len)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_POST, coap_next_id());
	if (err) {
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, GOLIOTH_SETTINGS_STATUS_PATH);
	if (err) {
		LOG_ERR("Unable add uri path to packet: %d", err);
		return err;
	}

	err = coap_append_option_int(&packet,
				     COAP_OPTION_CONTENT_FORMAT,
				     COAP_CONTENT_FORMAT_APP_JSON);
	if (err) {
		LOG_ERR("Unable add content format to packet: %d", err);
		return err;
	}

	err = golioth_send_coap_payload(client, &packet, coap_payload, coap_payload_len);
	if (err) {
		LOG_ERR("Error in golioth_send_coap: %d", err);
	}
	return err;
}

static int send_status_report(struct golioth_client *client,
			      int64_t version,
			      enum golioth_settings_status status)
{
	char response_buf[64];
	int nbytes = snprintf(response_buf,
		 sizeof(response_buf),
		 "{\"version\":%d,\"error_code\":%d}",
		 (int32_t)version, (int32_t)status);
	assert(nbytes > 0);

	LOG_HEXDUMP_DBG(response_buf, nbytes, "Response");

	return send_coap_response(client, response_buf, nbytes);
}

static void save_setting(const char *key, const struct golioth_settings_value *value)
{
	int err;
	char name[64] = {};

	/*
	 * Use the "gsettings" package in the Zephyr settings subsystem
	 * to store all settings.
	 */
	snprintf(name, sizeof(name), "gsettings/%s", key);

	if (IS_ENABLED(CONFIG_GOLIOTH_SETTINGS_PERSIST)) {
		LOG_INF("Saving setting: %s", name);
		err = settings_save_one(name, value, sizeof(struct golioth_settings_value));
		if (err) {
			LOG_ERR("Failed to save setting: %d", err);
			return;
		}
	}
}

static int on_setting(const struct coap_packet *response,
		      struct coap_reply *reply,
		      const struct sockaddr *from)
{
	struct golioth_client *client =
		CONTAINER_OF(reply, struct golioth_client, settings.observe_reply);

	uint16_t payload_len;
	const uint8_t *payload = coap_packet_get_payload(response, &payload_len);

	LOG_HEXDUMP_INF(payload, payload_len, "Payload");

	if (payload_len == 3 && payload[1] == 'O' && payload[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	UsefulBufC payload_buf = { payload, payload_len };

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

	/*
	 * We don't know how many items are in the settings map, so we will
	 * iterate until we get an error from QCBOR
	 */
	qerr = QCBORDecode_GetNext(&decode_ctx, &decoded_item);

	bool has_more_settings = (qerr == QCBOR_SUCCESS);
	enum golioth_settings_status cumulative_status = GOLIOTH_SETTINGS_SUCCESS;

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

		/*
		 * TODO - Add support for decoding integers
		 *
		 * Currently, the Golioth server encodes all numbers
		 * as doubles, but the following ticket will add support for encoding
		 * integers as integers (instead of double) in the payload:
		 *
		 * https://golioth.atlassian.net/browse/GB-346.
		 */
		if (data_type == QCBOR_TYPE_DOUBLE) {
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
		}

		if (data_type_valid) {
			enum golioth_settings_status setting_status =
				client->settings.callback(key, &value);

			if (setting_status != GOLIOTH_SETTINGS_SUCCESS) {
				cumulative_status = setting_status;
			} else {
				save_setting(key, &value);
			}
		}

		/* Get next item */
		qerr = QCBORDecode_GetNext(&decode_ctx, &decoded_item);
		has_more_settings = (qerr == QCBOR_SUCCESS);
	}

	QCBORDecode_ExitMap(&decode_ctx); /* settings */
	QCBORDecode_ExitMap(&decode_ctx); /* root */
	QCBORDecode_Finish(&decode_ctx);

	return send_status_report(client, version, cumulative_status);
}

static int golioth_settings_observe(struct golioth_client *client)
{
	struct coap_packet packet;
	uint8_t buffer[GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN];
	int err;

	err = coap_packet_init(&packet, buffer, sizeof(buffer),
			       COAP_VERSION_1, COAP_TYPE_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_GET, coap_next_id());
	if (err) {
		LOG_ERR("Failed to initialize packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_OBSERVE, 0);
	if (err) {
		LOG_ERR("Unable to add observe option");
		return err;
	}

	err = coap_packet_append_uri_path_from_stringz(&packet, GOLIOTH_SETTINGS_PATH);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		return err;
	}

	err = coap_append_option_int(&packet, COAP_OPTION_ACCEPT, COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		LOG_ERR("Unable add content format to packet");
		return err;
	}

	coap_reply_clear(&client->settings.observe_reply);
	coap_reply_init(&client->settings.observe_reply, &packet);
	client->settings.observe_reply.reply = on_setting;

	return golioth_send_coap(client, &packet);
}

static void on_message(struct golioth_client *client,
		       struct coap_packet *rx,
		       void *user_arg)
{
	coap_response_received(rx, NULL, &client->settings.observe_reply, 1);
}

int golioth_settings_register_callback(struct golioth_client *client,
				       golioth_settings_cb callback)
{
	if (!callback) {
		LOG_ERR("Callback must not be NULL");
		return -1;
	}

	/*
	 * One-time registration of callback.
	 *
	 * It's possible this function gets called again on a client reconnect,
	 * in which case we need to make sure not to re-register the callback.
	 */
	if (!client->settings.initialized) {
		int err = golioth_register_message_callback(client, on_message, NULL);

		if (err) {
			LOG_ERR("Failed to register message callback: %d", err);
			return err;
		}

		client->settings.callback = callback;
		client->settings.initialized = true;
	}

	return golioth_settings_observe(client);
}
