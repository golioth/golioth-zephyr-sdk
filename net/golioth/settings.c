/*
 * Copyright (c) 2022-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth/settings.h>
#include <net/golioth/system_client.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <stdio.h>
#include <assert.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"
#include "zcbor_utils.h"

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

#include "golioth_utils.h"

#define GOLIOTH_SETTINGS_PATH ".c"
#define GOLIOTH_SETTINGS_STATUS_PATH ".c/status"
#define GOLIOTH_SETTINGS_MAX_NAME_LEN 63 /* not including NULL */

struct settings_response {
	zcbor_state_t zse[1 /* num_backups */ + 2];
	uint8_t buf[CONFIG_GOLIOTH_SETTINGS_MAX_RESPONSE_LEN];
	size_t num_errors;
	struct golioth_client *client;
};

static int send_coap_response(struct golioth_client *client,
			      uint8_t *coap_payload,
			      size_t coap_payload_len)
{
	return golioth_coap_req_cb(client, COAP_METHOD_POST, PATHV(GOLIOTH_SETTINGS_STATUS_PATH),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   coap_payload, coap_payload_len,
				   golioth_req_rsp_default_handler, "Settings response ACK",
				   GOLIOTH_COAP_REQ_NO_RESP_BODY);
}

static void response_init(struct settings_response *response, struct golioth_client *client)
{
	memset(response, 0, sizeof(*response));

	response->client = client;

	zcbor_new_encode_state(response->zse, ARRAY_SIZE(response->zse),
			       response->buf, sizeof(response->buf), 1);

	/* Initialize the map */
	zcbor_map_start_encode(response->zse, SIZE_MAX /* TODO: really? */);
}

static void add_error_to_response(struct settings_response *response,
				  const char *key,
				  enum golioth_settings_status code)
{
	if (response->num_errors == 0) {
		zcbor_tstr_put_lit(response->zse, "errors");
		zcbor_list_start_encode(response->zse, SIZE_MAX);
	}

	zcbor_map_start_encode(response->zse, 2);

	zcbor_tstr_put_lit(response->zse, "setting_key");
	zcbor_tstr_put_term(response->zse, key);

	zcbor_tstr_put_lit(response->zse, "error_code");
	zcbor_int64_put(response->zse, code);

	zcbor_map_end_encode(response->zse, 2);

	response->num_errors++;
}

static int finalize_and_send_response(struct golioth_client *client,
				      struct settings_response *response,
				      int64_t version)
{
	bool ok;

	/*
	 * If there were errors, then the "errors" array is still open,
	 * so we need to close it.
	 */
	if (response->num_errors > 0) {
		ok = zcbor_list_end_encode(response->zse, SIZE_MAX);
		if (!ok) {
			return -ENOMEM;
		}
	}

	/* Set version */
	ok = zcbor_tstr_put_lit(response->zse, "version") &&
		zcbor_int64_put(response->zse, version);
	if (!ok) {
		return -ENOMEM;
	}

	/* Close the root map */
	ok = zcbor_map_end_encode(response->zse, 1);
	if (!ok) {
		return -ENOMEM;
	}

	LOG_HEXDUMP_DBG(response->buf, response->zse->payload - response->buf, "Response");

	return send_coap_response(client, response->buf, response->zse->payload - response->buf);
}

static int settings_decode(zcbor_state_t *zsd, void *value)
{
	struct settings_response *settings_response = value;
	struct golioth_client *client = settings_response->client;
	struct zcbor_string label;
	bool ok;

	ok = zcbor_map_start_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not start CBOR list correctly");
		return -EBADMSG;
	}

	while (!zcbor_list_or_map_end(zsd)) {
		/* Handle item */
		ok = zcbor_tstr_decode(zsd, &label);
		if (!ok) {
			LOG_WRN("Failed to get label");
			return -EBADMSG;
		}

		char key[GOLIOTH_SETTINGS_MAX_NAME_LEN + 1] = {};

		/* Copy setting label/name and ensure it's NULL-terminated */
		memcpy(key, label.value,
		       MIN(GOLIOTH_SETTINGS_MAX_NAME_LEN, label.len));

		bool data_type_valid = true;
		struct golioth_settings_value value = {};

		zcbor_major_type_t major_type = ZCBOR_MAJOR_TYPE(*zsd->payload);

		LOG_DBG("key = %s, major_type = %d", key, major_type);

		switch (major_type) {
		case ZCBOR_MAJOR_TYPE_TSTR: {
			struct zcbor_string str;

			ok = zcbor_tstr_decode(zsd, &str);
			if (ok) {
				value.type = GOLIOTH_SETTINGS_VALUE_TYPE_STRING;
				value.string.ptr = str.value;
				value.string.len = str.len;
			} else {
				LOG_ERR("Failed to parse tstr");
				data_type_valid = false;
			}
			break;
		}
		case ZCBOR_MAJOR_TYPE_PINT:
		case ZCBOR_MAJOR_TYPE_NINT: {
			ok = zcbor_int64_decode(zsd, &value.i64);
			if (ok) {
				value.type = GOLIOTH_SETTINGS_VALUE_TYPE_INT64;
			} else {
				LOG_ERR("Failed to parse int");
				data_type_valid = false;
			}
			break;
		}
		case ZCBOR_MAJOR_TYPE_SIMPLE: {
			double double_result;

			if (zcbor_float_decode(zsd, &double_result)) {
				value.f = (float)double_result;
				value.type = GOLIOTH_SETTINGS_VALUE_TYPE_FLOAT;
			} else if (zcbor_bool_decode(zsd, &value.b)) {
				value.type = GOLIOTH_SETTINGS_VALUE_TYPE_BOOL;
			} else {
				LOG_ERR("Failed to parse simple type");
				data_type_valid = false;
			}
			break;
		}
		default:
			LOG_ERR("Unrecognized data type: %d", major_type);
			data_type_valid = false;
			break;
		}

		if (data_type_valid) {
			enum golioth_settings_status setting_status =
				client->settings.callback(key, &value);

			if (setting_status != GOLIOTH_SETTINGS_SUCCESS) {
				add_error_to_response(settings_response, key, setting_status);
			}
		} else {
			add_error_to_response(settings_response,
					      key,
					      GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID);

			ok = zcbor_any_skip(zsd, NULL);
			if (!ok) {
				LOG_ERR("Failed to skip unsupported type");
				return -EBADMSG;
			}
		}
	}

	ok = zcbor_map_end_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not end CBOR list correctly");
		return -EBADMSG;
	}

	return 0;
}

static int on_setting(struct golioth_req_rsp *rsp)
{
	ZCBOR_STATE_D(zsd, 2, rsp->data, rsp->len, 1);
	struct golioth_client *client = rsp->user_data;
	int64_t version;
	struct settings_response settings_response;
	struct zcbor_map_entry map_entries[] = {
		ZCBOR_TSTR_LIT_MAP_ENTRY("settings", settings_decode, &settings_response),
		ZCBOR_TSTR_LIT_MAP_ENTRY("version", zcbor_map_int64_decode, &version),
	};
	int err;

	if (rsp->err) {
		LOG_ERR("Error on Settings observation: %d", rsp->err);
		return rsp->err;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, "Payload");

	if (rsp->len == 3 && rsp->data[1] == 'O' && rsp->data[2] == 'K') {
		/* Ignore "OK" response received after observing */
		return 0;
	}

	response_init(&settings_response, client);

	err = zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
	if (err) {
		LOG_ERR("Failed to parse tstr map");
		return err;
	}

	return finalize_and_send_response(client, &settings_response, version);
}

int golioth_settings_observe(struct golioth_client *client)
{
	return golioth_coap_req_cb(client, COAP_METHOD_GET, PATHV(GOLIOTH_SETTINGS_PATH),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
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

	return 0;
}
