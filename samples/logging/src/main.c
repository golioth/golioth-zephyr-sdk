/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>

#include <qcbor/qcbor.h>
#include <qcbor/qcbor_decode.h>
#include <qcbor/qcbor_spiffy_decode.h>

#include <logging/golioth.h>
#include <logging/log_ctrl.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static struct coap_reply coap_replies[1];

static const char * const severity_lvls[] = {
	"none",
	"err",
	"wrn",
	"inf",
	"dbg",
};

static int severity_level_get(UsefulBufC *severity)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(severity_lvls); i++) {
		if (strncmp(severity_lvls[i], severity->ptr, severity->len) == 0) {
			return i;
		}
	}

	return -1;
}

static int module_id_get(UsefulBufC *name)
{
	uint32_t modules_cnt = z_log_sources_count();

	for (uint32_t i = 0U; i < modules_cnt; i++) {
		const char *tmp_name = log_source_name_get(CONFIG_LOG_DOMAIN_ID, i);

		if (strncmp(tmp_name, name->ptr, name->len) == 0) {
			return i;
		}
	}

	return -1;
}

static int module_severity_set(UsefulBufC *module, UsefulBufC *severity)
{
	const struct log_backend *backend = log_backend_golioth_get();
	int severity_level = severity_level_get(severity);
	int module_id = module_id_get(module);

	if (severity_level < 0 || module_id < 0) {
		return -EINVAL;
	}

	LOG_INF("level %d module %d", severity_level, module_id);

	log_filter_set(backend, CONFIG_LOG_DOMAIN_ID, module_id, severity_level);

	return 0;
}

/*
 * This function is registed to be called when the data
 * stored at `/logs` changes.
 */
static int on_update(const struct coap_packet *response,
		     struct coap_reply *reply,
		     const struct sockaddr *from)
{
	QCBORDecodeContext decode_ctx;
	QCBORItem decoded_item;
	UsefulBufC payload;
	uint16_t payload_len;
	QCBORError qerr;
	int err;

	/*
	 * MAP:
	 * {"module": "level"}
	 */

	payload.ptr = coap_packet_get_payload(response, &payload_len);
	payload.len = payload_len;

	if (!payload.ptr) {
		LOG_WRN("packet did not contain data");
		return -ENOMSG;
	}

	LOG_HEXDUMP_DBG(payload.ptr, payload.len, "payload");

	QCBORDecode_Init(&decode_ctx, payload, QCBOR_DECODE_MODE_NORMAL);

	QCBORDecode_EnterMap(&decode_ctx, NULL);
	qerr = QCBORDecode_GetError(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_WRN("Did not enter CBOR map correctly");
		return -EBADMSG;
	}

	/* Iterate through all entries in map */
	while (true) {
		QCBORDecode_VGetNext(&decode_ctx, &decoded_item);

		qerr = QCBORDecode_GetError(&decode_ctx);
		if (qerr == QCBOR_ERR_NO_MORE_ITEMS) {
			break;
		}

		if (qerr != QCBOR_SUCCESS) {
			LOG_DBG("QCBORDecode_GetError: %d", qerr);
			break;
		}

		if (decoded_item.uLabelType != QCBOR_TYPE_TEXT_STRING) {
			LOG_WRN("Label type should be text string");
			continue;
		}

		if (decoded_item.uDataType != QCBOR_TYPE_TEXT_STRING) {
			LOG_WRN("Data type should be string");
			continue;
		}

		LOG_INF("Setting severity for module '%.*s' -> '%.*s'",
			decoded_item.label.string.len, (char *) decoded_item.label.string.ptr,
			decoded_item.val.string.len, (char *) decoded_item.val.string.ptr);

		/*
		 * Switch severity level of a module
		 */
		err = module_severity_set(&decoded_item.label.string, &decoded_item.val.string);
		if (err) {
			LOG_ERR("Failed to set: '%.*s' -> '%.*s'",
				decoded_item.label.string.len, (char *) decoded_item.label.string.ptr,
				decoded_item.val.string.len, (char *) decoded_item.val.string.ptr);
			continue;
		}
	}

	QCBORDecode_ExitMap(&decode_ctx);

	return 0;
}

/*
 * In the `main` function, this function is registed to be
 * called when the device connects to the Golioth server.
 */
static void golioth_on_connect(struct golioth_client *client)
{
	struct coap_reply *observe_reply;
	int err;

	coap_replies_clear(coap_replies, ARRAY_SIZE(coap_replies));

	observe_reply = coap_reply_next_unused(coap_replies,
					       ARRAY_SIZE(coap_replies));

	/*
	 * Observe the data stored at `/logs` in LightDB.
	 * When that data is updated, the `on_update` callback
	 * will be called.
	 * This will get the value when first called, even if
	 * the value doesn't change.
	 */
	err = golioth_lightdb_observe(client,
				      GOLIOTH_LIGHTDB_PATH("logs"),
				      COAP_CONTENT_FORMAT_APP_CBOR,
				      observe_reply, on_update);

	if (err) {
		LOG_WRN("failed to observe lightdb path: %d", err);
	}
}

/*
 * In the `main` function, this function is registed to be
 * called when the device receives a packet from the Golioth server.
 */
static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	/*
	 * In order for the observe callback to be called,
	 * we need to call this function.
	 */
	coap_response_received(rx, NULL, coap_replies,
			       ARRAY_SIZE(coap_replies));
}

static void func_1(int counter)
{
	LOG_DBG("Log 1: %d", counter);
}

static void func_2(int counter)
{
	LOG_DBG("Log 2: %d", counter);
}

void main(void)
{
	int counter = 0;

	LOG_DBG("Start Logging sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;
	golioth_system_client_start();

	while (true) {
		LOG_DBG("Debug info! %d", counter);
		func_1(counter);
		func_2(counter);
		LOG_WRN("Warn: %d", counter);
		LOG_ERR("Err: %d", counter);
		LOG_HEXDUMP_INF(&counter, sizeof(counter), "Counter hexdump");

		counter++;

		k_sleep(K_SECONDS(5));
	}
}
