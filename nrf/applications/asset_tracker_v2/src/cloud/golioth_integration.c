/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <modem/at_cmd.h>
#include <net/coap.h>
#include <net/golioth/system_client.h>

#include "cloud/cloud_wrapper.h"

#ifdef CONFIG_GOLIOTH_CODEC_CBOR
#define COAP_CONTENT_FORMAT	COAP_CONTENT_FORMAT_APP_CBOR
#else
#define COAP_CONTENT_FORMAT	COAP_CONTENT_FORMAT_APP_JSON
#endif

#define MODULE golioth_integration

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CLOUD_INTEGRATION_LOG_LEVEL);

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static struct coap_reply coap_replies[4];

static cloud_wrap_evt_handler_t wrapper_evt_handler;

static void cloud_wrapper_notify_event(const struct cloud_wrap_event *evt)
{
	if ((wrapper_evt_handler != NULL) && (evt != NULL)) {
		wrapper_evt_handler(evt);
	} else {
		LOG_ERR("Library event handler not registered, or empty event");
	}
}

static int on_cfg_update(const struct coap_packet *response,
			 struct coap_reply *reply,
			 const struct sockaddr *from)
{
	struct cloud_wrap_event cloud_wrap_evt = { 0 };
	uint16_t payload_len;
	const uint8_t *payload;

	payload = coap_packet_get_payload(response, &payload_len);
	if (!payload) {
		LOG_WRN("packet did not contain data");
		return -ENOMSG;
	}

	if (IS_ENABLED(CONFIG_CLOUD_INTEGRATION_LOG_LEVEL)) {
		LOG_HEXDUMP_DBG(payload, payload_len, "payload");
	}

	cloud_wrap_evt.type = CLOUD_WRAP_EVT_DATA_RECEIVED;
	cloud_wrap_evt.data.buf = (void *)payload;
	cloud_wrap_evt.data.len = payload_len;

	cloud_wrapper_notify_event(&cloud_wrap_evt);

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	struct cloud_wrap_event cloud_wrap_evt = { 0 };
	struct coap_reply *observe_reply;
	int err;

	coap_replies_clear(coap_replies, ARRAY_SIZE(coap_replies));

	cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTED;
	cloud_wrapper_notify_event(&cloud_wrap_evt);

	observe_reply = coap_reply_next_unused(coap_replies,
					       ARRAY_SIZE(coap_replies));
	if (!observe_reply) {
		LOG_ERR("cannot allocate observe_reply object");
		return;
	}

	err = golioth_lightdb_observe(client,
				      GOLIOTH_LIGHTDB_PATH("cfg"),
				      COAP_CONTENT_FORMAT,
				      observe_reply, on_cfg_update);
	if (err) {
		LOG_ERR("golioth_lightdb_observe, error: %d", err);
		return;
	}
}

static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	coap_response_received(rx, NULL, coap_replies,
			       ARRAY_SIZE(coap_replies));
}

int cloud_wrap_init(cloud_wrap_evt_handler_t event_handler)
{
	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;

	LOG_DBG("********************************************");
	LOG_DBG(" The Asset Tracker v2 has started");
	LOG_DBG(" Version:      %s", CONFIG_ASSET_TRACKER_V2_APP_VERSION);
	LOG_DBG(" Cloud:        %s", "Golioth");
	LOG_DBG("********************************************");

	wrapper_evt_handler = event_handler;

	return 0;
}

int cloud_wrap_connect(void)
{
	static bool connect_issued = false;

	if (connect_issued) {
		LOG_WRN("connect is supported only once!");
		return 0;
	}

	connect_issued = true;

	golioth_system_client_start();

	return 0;
}

int cloud_wrap_disconnect(void)
{
	LOG_WRN("disconnect not supported!");

	return 0;
}

int cloud_wrap_state_get(void)
{
	LOG_WRN("state get! what should we do here?");

	return 0;
}

int cloud_wrap_state_send(char *buf, size_t len)
{
	int err;

	err = golioth_lightdb_set(client,
				  GOLIOTH_LIGHTDB_PATH(""),
				  COAP_CONTENT_FORMAT,
				  buf, len);
	if (err) {
		LOG_ERR("golioth_lightdb_set, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_data_send(char *buf, size_t len)
{
	int err;

	err = golioth_lightdb_set(client,
				  GOLIOTH_LIGHTDB_STREAM_PATH("telemetry"),
				  COAP_CONTENT_FORMAT,
				  buf, len);
	if (err) {
		LOG_ERR("golioth_lightdb_set, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_batch_send(char *buf, size_t len)
{
	int err;

	err = golioth_lightdb_set(client,
				  GOLIOTH_LIGHTDB_STREAM_PATH("telemetry"),
				  COAP_CONTENT_FORMAT,
				  buf, len);
	if (err) {
		LOG_ERR("golioth_lightdb_set, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_ui_send(char *buf, size_t len)
{
	int err;

	err = golioth_lightdb_set(client,
				  GOLIOTH_LIGHTDB_STREAM_PATH("telemetry"),
				  COAP_CONTENT_FORMAT,
				  buf, len);
	if (err) {
		LOG_ERR("golioth_lightdb_set, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_neighbor_cells_send(char *buf, size_t len)
{
	/* Not supported */
	return -ENOTSUP;
}
