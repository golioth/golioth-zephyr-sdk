/*
 * Copyright (c) 2021-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_basics, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <net/golioth/rpc.h>
#include <samples/common/net_connect.h>
#include <zephyr/net/coap.h>

#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>

#include "dfu.h"
#include "lightdb_helpers.h"

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

/* Configurable via LightDB State at path "desired/my_config" */
static int32_t _my_config = 0;

/* Configurable via Settings service, key = "LOOP_DELAY_S" */
static int32_t _loop_delay_s = 10;

static K_SEM_DEFINE(connected, 0, 1);

static int on_loop_delay_setting(int32_t new_value)
{
	LOG_INF("Setting loop delay to %" PRId32 " s", new_value);

	_loop_delay_s = new_value;

	return 0;
}

enum golioth_settings_status on_setting(const char *key,
					const struct golioth_settings_value *value)
{
	LOG_DBG("Received setting: key = %s, type = %d", key, value->type);
	if (strcmp(key, "LOOP_DELAY_S") == 0) {
		/* This setting is expected to be numeric, return an error if it's not */
		if (value->type != GOLIOTH_SETTINGS_VALUE_TYPE_INT64) {
			return GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID;
		}

		/* Setting has passed all checks, so apply it to the loop delay */
		(void)on_loop_delay_setting(value->i64);

		return GOLIOTH_SETTINGS_SUCCESS;
	}

	/* If the setting is not recognized, we should return an error */
	return GOLIOTH_SETTINGS_KEY_NOT_RECOGNIZED;
}

static enum golioth_rpc_status on_multiply(QCBORDecodeContext *request_params_array,
					   QCBOREncodeContext *response_detail_map,
					   void *callback_arg)
{
	double a, b;
	int64_t value;
	QCBORError qerr;

	QCBORDecode_GetDouble(request_params_array, &a);
	QCBORDecode_GetDouble(request_params_array, &b);
	qerr = QCBORDecode_GetError(request_params_array);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to decode array items: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return GOLIOTH_RPC_INVALID_ARGUMENT;
	}

	value = a * b;
	QCBOREncode_AddInt64ToMap(response_detail_map, "value", value);
	return GOLIOTH_RPC_OK;
}

static int golioth_req_rsp_as_cbor_int64(struct golioth_req_rsp *rsp, int64_t *value)
{
	QCBORDecodeContext dec;
	QCBORError qerr;
	UsefulBufC payload_buf = { rsp->data, rsp->len };

	QCBORDecode_Init(&dec, payload_buf, QCBOR_DECODE_MODE_NORMAL);
	QCBORDecode_GetInt64(&dec, value);
	qerr = QCBORDecode_GetError(&dec);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to parse int64_t: %d (%s)",
			qerr, qcbor_err_to_str(qerr));
		QCBORDecode_Finish(&dec);
		return qcbor_error_to_posix(qerr);
	}

	return 0;
}

static int on_get_my_int(struct golioth_req_rsp *rsp)
{
	const char *path = rsp->user_data;
	int64_t value;
	int err;

	if (rsp->err) {
		LOG_ERR("Failed to receive %s value: %d", path, rsp->err);
		return rsp->err;
	}

	err = golioth_req_rsp_as_cbor_int64(rsp, &value);
	if (err) {
		LOG_ERR("Failed to parse '%s': %d", path, err);
		return err;
	}

	LOG_INF("Callback got %s = %" PRId64, path, value);

	return 0;
}

static int on_my_config(struct golioth_req_rsp *rsp)
{
	const char *path = rsp->user_data;
	int64_t desired_value;
	int err;

	if (rsp->err) {
		LOG_ERR("Failed to receive '%s' value: %d", path, rsp->err);
		return rsp->err;
	}

	err = golioth_req_rsp_as_cbor_int64(rsp, &desired_value);
	if (err) {
		LOG_ERR("Failed to parse '%s': %d", path, err);
		return err;
	}

	LOG_INF("Cloud desires %s = %" PRId64 ". Setting now.",
		path, desired_value);

	_my_config = desired_value;

	err = golioth_lightdb_delete_cb(client, path, NULL, NULL);
	if (err) {
		LOG_WRN("Failed to delete '%s' from LightDB: %d", path, err);
	}

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	int err;

	LOG_INF("Golioth client %s", "connected");

	k_sem_give(&connected);

	err = golioth_rpc_observe(client);
	if (err) {
		LOG_ERR("Failed to observe RPC: %d", err);
	}

	err = golioth_settings_observe(client);
	if (err) {
		LOG_ERR("Failed to observe settings: %d", err);
	}

	err = golioth_lightdb_observe_cb(client, "desired/my_config",
					 GOLIOTH_CONTENT_FORMAT_APP_CBOR,
					 on_my_config, "desired/my_config");
	if (err) {
		LOG_ERR("Failed to observe LightDB path: %d", err);
	}

	dfu_on_connect(client);
}

static void counter_set_async(int counter)
{
	char sbuf[sizeof("4294967295")];
	int err;

	snprintk(sbuf, sizeof(sbuf) - 1, "%d", counter);

	err = golioth_lightdb_set_cb(client, "counter",
				     GOLIOTH_CONTENT_FORMAT_APP_JSON,
				     sbuf, strlen(sbuf),
				     NULL, NULL);
	if (err) {
		LOG_WRN("Failed to set counter: %d", err);
		return;
	}
}

void main(void)
{
	int counter = 0;
	int err;

	LOG_DBG("Start Hello sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	err = golioth_rpc_register(client, "multiply", on_multiply, NULL);
	if (err) {
		LOG_ERR("Failed to register 'multiply' RPC method: %d", err);
	}

	err = golioth_settings_register_callback(client, on_setting);
	if (err) {
		LOG_ERR("Failed to register settings callback: %d", err);
	}

	k_sem_take(&connected, K_FOREVER);

	LOG_INF("Hello, Golioth!");

	// For OTA, we will spawn a background task that will listen for firmware
	// updates from Golioth and automatically update firmware on the device
	/* NOTE: this is done in dfu_main() */

	// There are a number of different functions you can call to get and set values in
	// LightDB state, based on the type of value (e.g. int, bool, float, string, JSON).
	//
	// This is an "asynchronous" function, meaning that the function will return
	// immediately and the integer will be sent to Golioth at a later time.
	// Internally, the request is added to a queue which is processed
	// by the Golioth client task.
	//
	// Any functions provided by this SDK ending in _async behave the same way.
	//
	// The last two arguments are for an optional callback function and argument,
	// in case the user wants to be notified when the set request has completed
	// and received acknowledgement from the Golioth server. In this case
	// we set them to NULL, which makes this a "fire-and-forget" request.
	err = golioth_lightdb_set_cb(client, "my_int",
				     GOLIOTH_CONTENT_FORMAT_APP_JSON,
				     "42", sizeof("42") - 1,
				     NULL, NULL);
	if (err) {
		LOG_WRN("Failed to set '%s': %d", "my_int", err);
	}

	// We can also send requests "synchronously", meaning the function will block
	// until one of 3 things happen (whichever comes first):
	//
	//  1. We receive a response to the request from the server
	//  2. The user-provided timeout expires
	//  3. The default client task timeout expires (GOLIOTH_COAP_RESPONSE_TIMEOUT_S)
	//
	// In this case, we will block for up to 2 seconds waiting for the server response.
	// We'll check the return code to know whether a timeout happened.
	//
	// Any function provided by this SDK ending in _sync will have the same meaning.
	err = golioth_lightdb_set(client, "my_string",
				  GOLIOTH_CONTENT_FORMAT_APP_JSON,
				  "\"asdf\"", sizeof("\"asdf\"") - 1);
	if (err) {
		LOG_ERR("Error setting string: %d", err);
	}

	// Read back the integer we set above
	int32_t readback_int = 0;
	err = golioth_lightdb_get_auto(client, "my_int", &readback_int);
	if (err) {
		LOG_ERR("Synchronous get my_int failed: %d", err);
	} else {
		LOG_INF("Synchronously got my_int = %" PRId32, readback_int);
	}

	// To asynchronously get a value from LightDB, a callback function must be provided
	err = golioth_lightdb_get_cb(client, "my_int",
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     on_get_my_int, "my_int");
	if (err) {
		LOG_WRN("Failed to get data from LightDB: %d", err);
	}

	// We can also "observe" paths in LightDB state. The Golioth cloud will notify
	// our client whenever the resource at that path changes, without needing
	// to poll.
	//
	// This can be used to implement the "digital twin" concept that is common in IoT.
	//
	// In this case, we will observe the path desired/my_config for changes.
	// The callback will read the value, update it locally, then delete the path
	// to indicate that the desired state was processed (the "twins" should be
	// in sync at that point).
	//
	// If you want to try this out, log into Golioth console (console.golioth.io),
	// go to the "LightDB State" tab, and add a new item for desired/my_config.
	// Once set, the on_my_config callback function should be called here.
	/* NOTE: this is done in client->on_connect() */

	// LightDB Stream functions are nearly identical to LightDB state.
	err = golioth_stream_push_cb(client, "my_stream_int",
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     "15", sizeof("15") - 1,
				     NULL, NULL);
	if (err) {
		LOG_WRN("Failed to push temperature: %d", err);
		return;
	}

	// We can register a callback for persistent settings. The Settings service
	// allows remote users to manage and push settings to devices.
	/* NOTE: this is done in client->on_connect() */

	// Now we'll just sit in a loop and update a LightDB state variable every
	// once in a while.
	LOG_INF("Entering endless loop");

	for (int i = 0; i < 5; i++) {
		LOG_INF("Sending hello! %d", counter);

		counter_set_async(counter);
		counter++;

		k_sleep(K_SECONDS(_loop_delay_s));
	}

	dfu_main();
}
