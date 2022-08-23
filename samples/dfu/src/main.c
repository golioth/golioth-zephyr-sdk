/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_dfu, LOG_LEVEL_DBG);

#include <net/golioth/fw.h>
#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>

#include <stdlib.h>
#include <stdio.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/coap.h>
#include <zephyr/sys/reboot.h>

#include "flash.h"

#define REBOOT_DELAY_SEC	1

static void reboot_handler(struct k_work *work)
{
	sys_reboot(SYS_REBOOT_COLD);
}

K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_handler);

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

struct dfu_ctx {
	struct flash_img_context flash;
	char version[65];
};

static struct dfu_ctx update_ctx;
static enum golioth_dfu_result dfu_initial_result = GOLIOTH_DFU_RESULT_INITIAL;

static int data_received(struct golioth_req_rsp *rsp)
{
	struct dfu_ctx *dfu = rsp->user_data;
	bool last = rsp->get_next == NULL;
	int err;

	if (rsp->err) {
		LOG_ERR("Error while receiving FW data: %d", rsp->err);
		return 0;
	}

	LOG_DBG("Received %zu bytes at offset %zu%s", rsp->len, rsp->off,
		last ? " (last)" : "");

	if (rsp->off == 0) {
		err = flash_img_prepare(&dfu->flash);
		if (err) {
			return err;
		}
	}

	err = flash_img_buffered_write(&dfu->flash, rsp->data, rsp->len, last);
	if (err) {
		LOG_ERR("Failed to write to flash: %d", err);
		return err;
	}

	if (last) {
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

		k_work_schedule(&reboot_work, K_SECONDS(REBOOT_DELAY_SEC));
	}

	if (rsp->get_next) {
		rsp->get_next(rsp->get_next_data, 0);
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

static int golioth_desired_update(struct golioth_req_rsp *rsp)
{
	struct dfu_ctx *dfu = rsp->user_data;
	size_t version_len = sizeof(dfu->version) - 1;
	uint8_t uri[64];
	uint8_t *uri_p;
	size_t uri_len = sizeof(uri);
	int err;

	if (rsp->err) {
		LOG_ERR("Error while receiving desired FW update: %d", rsp->err);
		return 0;
	}

	LOG_HEXDUMP_DBG(rsp->data, rsp->len, "Desired");

	err = golioth_fw_desired_parse(rsp->data, rsp->len,
				       dfu->version, &version_len,
				       uri, &uri_len);
	switch (err) {
	case 0:
		break;
	case -ENOENT:
		LOG_INF("No release rolled out yet");
		return 0;
	default:
		LOG_ERR("Failed to parse desired version: %d", err);
		return err;
	}

	dfu->version[version_len] = '\0';

	if (version_len == strlen(current_version_str) &&
	    !strncmp(current_version_str, dfu->version, version_len)) {
		LOG_INF("Desired version (%s) matches current firmware version!",
			current_version_str);
		return -EALREADY;
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

	err = golioth_fw_download(client, uri_p, uri_len, data_received, dfu);
	if (err) {
		LOG_ERR("Failed to request firmware: %d", err);
		return err;
	}

	return 0;
}

static void golioth_on_connect(struct golioth_client *client)
{
	int err;

	err = golioth_fw_report_state(client, "main",
				      current_version_str,
				      NULL,
				      GOLIOTH_FW_STATE_IDLE,
				      dfu_initial_result);
	if (err) {
		LOG_ERR("Failed to report firmware state: %d", err);
	}

	err = golioth_fw_observe_desired(client, golioth_desired_update, &update_ctx);
	if (err) {
		LOG_ERR("Failed to start observation of desired FW: %d", err);
	}
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

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();
}
