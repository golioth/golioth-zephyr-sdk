/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_lightdb, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>
#include <zephyr/net/coap.h>

#include <zephyr/drivers/gpio.h>
#include <stdlib.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_decode.h>
#include <qcbor/qcbor_spiffy_decode.h>

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

#define LED_GPIO_SPEC(i, _)						\
	COND_CODE_1(DT_NODE_HAS_STATUS(DT_ALIAS(led##i), okay),		\
		    (GPIO_DT_SPEC_GET(DT_ALIAS(led##i), gpios),),	\
		    ())

static struct gpio_dt_spec led[] = {
	LISTIFY(10, LED_GPIO_SPEC, ())
};

static void golioth_led_initialize(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(led); i++) {
		gpio_pin_configure_dt(&led[i], GPIO_OUTPUT_INACTIVE);
	}
}

static void golioth_led_set(unsigned int id, bool value)
{
	if (id >= ARRAY_SIZE(led)) {
		LOG_WRN("There is no LED %u (total %zu)", id,
			(size_t) ARRAY_SIZE(led));
		return;
	}

	gpio_pin_set(led[id].port, led[id].pin, value);
}

static void golioth_led_set_by_name(const char *name, bool value)
{
	char *endptr;
	unsigned long id;

	id = strtoul(name, &endptr, 0);
	if (endptr == name || *endptr != '\0') {
		LOG_WRN("LED name '%s' is not valid", name);
		return;
	}

	golioth_led_set(id, value);
}

static int golioth_led_handle(struct golioth_req_rsp *rsp)
{
	QCBORDecodeContext decode_ctx;
	QCBORItem decoded_item;
	UsefulBufC payload = { rsp->data, rsp->len };
	QCBORError qerr;
	char name[5];
	bool value;

	if (rsp->err) {
		LOG_ERR("Failed to receive led value: %d", rsp->err);
		return rsp->err;
	}

	QCBORDecode_Init(&decode_ctx, payload, QCBOR_DECODE_MODE_NORMAL);

	/*
	 * Expect map of "text string" (label) -> boolean (value) entries.
	 *
	 * Example 1:
	 * {
	 *     "0": true,
	 *     "1": false
	 * }
	 * means that:
	 * - LED 0 is expected to be switched on,
	 * - LED 1 is expected to be switched off.
	 *
	 * Example 2:
	 * {
	 *     "0": false,
	 *     "4": true,
	 *     "6": false.
	 * }
	 * means that:
	 * - LED 0 is expected to be switched off,
	 * - LED 4 is expected to be switched on,
	 * - LED 6 is expected to be switched off.
	 */
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
			/* Reset decoding error, as "no more items" was expected */
			QCBORDecode_GetAndResetError(&decode_ctx);
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

		if (decoded_item.uDataType != QCBOR_TYPE_FALSE &&
		    decoded_item.uDataType != QCBOR_TYPE_TRUE) {
			LOG_WRN("Data type should be boolean");
			continue;
		}

		if (decoded_item.label.string.len > sizeof(name) - 1) {
			LOG_HEXDUMP_WRN(decoded_item.label.string.ptr,
					decoded_item.label.string.len,
					"Too long label");
			continue;
		}

		/* Copy label to NULL-terminated string */
		memcpy(name, decoded_item.label.string.ptr, decoded_item.label.string.len);
		name[decoded_item.label.string.len] = '\0';

		value = (decoded_item.uDataType == QCBOR_TYPE_TRUE);

		LOG_INF("LED %s -> %s", name, value ? "ON" : "OFF");

		/*
		 * Switch on/off requested LED based on label (LED name/id) and
		 * value (requested LED state).
		 */
		golioth_led_set_by_name(name, value);
	}

	QCBORDecode_ExitMap(&decode_ctx);

	qerr = QCBORDecode_Finish(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_WRN("Failed to finish decoding: %d (%s)", qerr, qcbor_err_to_str(qerr));
	}

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	int err;

	err = golioth_lightdb_observe_cb(client, "led",
					 GOLIOTH_CONTENT_FORMAT_APP_CBOR,
					 golioth_led_handle, NULL);
	if (err) {
		LOG_WRN("failed to observe lightdb path: %d", err);
	}
}

int main(void)
{
	LOG_DBG("Start LightDB LED sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	golioth_led_initialize();

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	while (true) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
