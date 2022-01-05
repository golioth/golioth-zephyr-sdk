/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_dfu, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/golioth/fw.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>

#include <logging/log_ctrl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/reboot.h>

#include "flash.h"

#define REBOOT_DELAY_SEC	1

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static struct coap_reply coap_replies[4];

struct dfu_ctx {
	struct golioth_fw_download_ctx fw_ctx;
	struct flash_img_context flash;
	char version[65];
};

static struct dfu_ctx update_ctx;
static enum golioth_dfu_result dfu_initial_result = GOLIOTH_DFU_RESULT_INITIAL;

static int data_received(struct golioth_blockwise_download_ctx *ctx,
			 const uint8_t *data, size_t offset, size_t len,
			 bool last)
{
	struct dfu_ctx *dfu = CONTAINER_OF(ctx, struct dfu_ctx, fw_ctx);
	int err;

	LOG_DBG("Received %zu bytes at offset %zu%s", len, offset,
		last ? " (last)" : "");

	if (offset == 0) {
		err = flash_img_prepare(&dfu->flash);
		if (err) {
			return err;
		}
	}

	err = flash_img_buffered_write(&dfu->flash, data, len, last);
	if (err) {
		LOG_ERR("Failed to write to flash: %d", err);
		return err;
	}

	if (offset > 0 && last) {
		err = golioth_fw_report_state(client, "main",
					      current_version_str,
					      dfu->version,
					      GOLIOTH_FW_STATE_DOWNLOADED,
					      GOLIOTH_DFU_RESULT_INITIAL);
		if (err) {
			LOG_ERR("Failed to update to '%s' state: %d", "downloaded", err);
		}

		err = golioth_fw_report_state(client, "main",
					      current_version_str,
					      dfu->version,
					      GOLIOTH_FW_STATE_UPDATING,
					      GOLIOTH_DFU_RESULT_INITIAL);
		if (err) {
			LOG_ERR("Failed to update to '%s' state: %d", "updating", err);
		}

		LOG_INF("Requesting upgrade");

		err = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (err) {
			LOG_ERR("Failed to request upgrade: %d", err);
			return err;
		}

		LOG_INF("Rebooting in %d second(s)", REBOOT_DELAY_SEC);

		/* Synchronize logs */
		LOG_PANIC();

		k_sleep(K_SECONDS(REBOOT_DELAY_SEC));

		sys_reboot(SYS_REBOOT_COLD);
	}

	return 0;
}

static uint8_t *uri_strip_leading_slash(uint8_t *uri, size_t *uri_len)
{
	if (*uri_len > 0 && uri[0] == '/') {
		(*uri_len)--;
		return &uri[1];
	}

	return uri;
}

static int golioth_desired_update(const struct coap_packet *update,
				  struct coap_reply *reply,
				  const struct sockaddr *from)
{
	struct dfu_ctx *dfu = &update_ctx;
	struct coap_reply *fw_reply;
	const uint8_t *payload;
	uint16_t payload_len;
	size_t version_len = sizeof(dfu->version) - 1;
	uint8_t uri[64];
	uint8_t *uri_p;
	size_t uri_len = sizeof(uri);
	int err;

	payload = coap_packet_get_payload(update, &payload_len);
	if (!payload) {
		LOG_ERR("No payload in CoAP!");
		return -EIO;
	}

	LOG_HEXDUMP_DBG(payload, payload_len, "Desired");

	err = golioth_fw_desired_parse(payload, payload_len,
				       dfu->version, &version_len,
				       uri, &uri_len);
	if (err) {
		LOG_ERR("Failed to parse desired version: %d", err);
		return err;
	}

	dfu->version[version_len] = '\0';

	if (version_len == strlen(current_version_str) &&
	    !strncmp(current_version_str, dfu->version, version_len)) {
		LOG_INF("Desired version (%s) matches current firmware version!",
			log_strdup(current_version_str));
		return -EALREADY;
	}

	fw_reply = coap_reply_next_unused(coap_replies, ARRAY_SIZE(coap_replies));
	if (!reply) {
		LOG_ERR("No more reply handlers");
		return -ENOMEM;
	}

	uri_p = uri_strip_leading_slash(uri, &uri_len);

	err = golioth_fw_report_state(client, "main",
				      current_version_str,
				      dfu->version,
				      GOLIOTH_FW_STATE_DOWNLOADING,
				      GOLIOTH_DFU_RESULT_INITIAL);
	if (err) {
		LOG_ERR("Failed to update to '%s' state: %d", "downloading", err);
	}

	err = golioth_fw_download(client, &dfu->fw_ctx, uri_p, uri_len,
				  fw_reply, data_received);
	if (err) {
		LOG_ERR("Failed to request firmware: %d", err);
		return err;
	}

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	struct coap_reply *reply;
	int err;
	int i;

	err = golioth_fw_report_state(client, "main",
				      current_version_str,
				      NULL,
				      GOLIOTH_FW_STATE_IDLE,
				      dfu_initial_result);
	if (err) {
		LOG_ERR("Failed to report firmware state: %d", err);
	}

	for (i = 0; i < ARRAY_SIZE(coap_replies); i++) {
		coap_reply_clear(&coap_replies[i]);
	}

	reply = coap_reply_next_unused(coap_replies, ARRAY_SIZE(coap_replies));
	if (!reply) {
		LOG_ERR("No more reply handlers");
	}

	err = golioth_fw_observe_desired(client, reply, golioth_desired_update);
	if (err) {
		coap_reply_clear(reply);
	}
}

static void golioth_on_message(struct golioth_client *client,
			       struct coap_packet *rx)
{
	uint16_t payload_len;
	const uint8_t *payload;
	uint8_t type;

	type = coap_header_get_type(rx);
	payload = coap_packet_get_payload(rx, &payload_len);

	(void)coap_response_received(rx, NULL, coap_replies,
				     ARRAY_SIZE(coap_replies));
}

void main(void)
{
	int err;

	LOG_DBG("Start DFU sample");

	if (!boot_is_img_confirmed()) {
		/*
		 * There is no shared context between previous update request
		 * and current boot, so treat current image 'confirmed' flag as
		 * an indication whether previous update process was successful
		 * or not.
		 */
		dfu_initial_result = GOLIOTH_DFU_RESULT_FIRMWARE_UPDATED_SUCCESSFULLY;

		err = boot_write_img_confirmed();
		if (err) {
			LOG_ERR("Failed to confirm image: %d", err);
		}
	}

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;
	golioth_system_client_start();
}
