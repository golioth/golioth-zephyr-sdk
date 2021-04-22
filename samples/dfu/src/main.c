/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_dfu, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <net/golioth/system_client.h>

#include <dfu/flash_img.h>
#include <dfu/mcuboot.h>
#include <logging/log_ctrl.h>
#include <power/reboot.h>
#include <stdlib.h>
#include <storage/flash_map.h>

#include "wifi.h"

#define REBOOT_DELAY_SEC	1

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static struct coap_reply coap_replies[1];

struct dfu_ctx {
	struct golioth_blockwise_observe_ctx observe;
	struct flash_img_context flash;
};

static struct dfu_ctx update_ctx;

/**
 * Determines if the specified area of flash is completely unwritten.
 *
 * @note This is a copy of zephyr_img_mgmt_flash_check_empty() from mcumgr.
 */
static int flash_area_check_empty(const struct flash_area *fa,
				  bool *out_empty)
{
	uint32_t data[16];
	off_t addr;
	off_t end;
	int bytes_to_read;
	int rc;
	int i;

	__ASSERT_NO_MSG(fa->fa_size % 4 == 0);

	end = fa->fa_size;
	for (addr = 0; addr < end; addr += sizeof(data)) {
		if (end - addr < sizeof(data)) {
			bytes_to_read = end - addr;
		} else {
			bytes_to_read = sizeof(data);
		}

		rc = flash_area_read(fa, addr, data, bytes_to_read);
		if (rc != 0) {
			flash_area_close(fa);
			return rc;
		}

		for (i = 0; i < bytes_to_read / 4; i++) {
			if (data[i] != 0xffffffff) {
				*out_empty = false;
				flash_area_close(fa);
				return 0;
			}
		}
	}

	*out_empty = true;

	return 0;
}

static int flash_img_erase_if_needed(struct flash_img_context *ctx)
{
	bool empty;
	int err;

	if (IS_ENABLED(CONFIG_IMG_ERASE_PROGRESSIVELY)) {
		return 0;
	}

	err = flash_area_check_empty(ctx->flash_area, &empty);
	if (err) {
		return err;
	}

	if (empty) {
		return 0;
	}

	err = flash_area_erase(ctx->flash_area, 0, ctx->flash_area->fa_size);
	if (err) {
		return err;
	}

	return 0;
}

static const char *swap_type_str(int swap_type)
{
	switch (swap_type) {
	case BOOT_SWAP_TYPE_NONE:
		return "none";
	case BOOT_SWAP_TYPE_TEST:
		return "test";
	case BOOT_SWAP_TYPE_PERM:
		return "perm";
	case BOOT_SWAP_TYPE_REVERT:
		return "revert";
	case BOOT_SWAP_TYPE_FAIL:
		return "fail";
	}

	return "unknown";
}

static int flash_img_prepare(struct flash_img_context *flash)
{
	int swap_type;
	int err;

	swap_type = mcuboot_swap_type();
	switch (swap_type) {
	case BOOT_SWAP_TYPE_REVERT:
		LOG_WRN("'revert' swap type detected, it is not safe to continue");
		return -EBUSY;
	default:
		LOG_INF("swap type: %s", swap_type_str(swap_type));
		break;
	}

	err = flash_img_init(flash);
	if (err) {
		LOG_ERR("failed to init: %d", err);
		return err;
	}

	err = flash_img_erase_if_needed(flash);
	if (err) {
		LOG_ERR("failed to erase: %d", err);
		return err;
	}

	return 0;
}

static int data_received(struct golioth_blockwise_observe_ctx *observe,
			 const uint8_t *data, size_t offset, size_t len,
			 bool last)
{
	struct dfu_ctx *dfu = CONTAINER_OF(observe, struct dfu_ctx, observe);
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

static void golioth_on_connect(struct golioth_client *client)
{
	struct coap_reply *reply;
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(coap_replies); i++) {
		coap_reply_clear(&coap_replies[i]);
	}

	reply = coap_reply_next_unused(coap_replies, ARRAY_SIZE(coap_replies));
	if (!reply) {
		LOG_ERR("No more reply handlers");
	}

	err = golioth_observe_blockwise(client, &update_ctx.observe, "update",
					reply, data_received);
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

	err = boot_write_img_confirmed();
	if (err) {
		LOG_ERR("Failed to confirm image: %d", err);
	}

	if (IS_ENABLED(CONFIG_NET_L2_WIFI_MGMT)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect();
	}

	client->on_connect = golioth_on_connect;
	client->on_message = golioth_on_message;
	golioth_system_client_start();

	while (true) {
		err = golioth_send_hello(client);
		if (err) {
			LOG_WRN("Failed to send hello: %d", err);
		}

		k_sleep(K_SECONDS(5));
	}
}
