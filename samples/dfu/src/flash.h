/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __APP_FLASH_H__
#define __APP_FLASH_H__

#ifdef CONFIG_BOOTLOADER_MCUBOOT

#include <dfu/flash_img.h>
#include <dfu/mcuboot.h>
#include <zephyr/types.h>

int flash_img_prepare(struct flash_img_context *flash);

extern char current_version_str[sizeof("255.255.65535")];

#else /* CONFIG_BOOTLOADER_MCUBOOT */

#include <stddef.h>
#include <stdint.h>

struct flash_img_context {
	/* empty */
};

static inline int flash_img_prepare(struct flash_img_context *flash)
{
	return 0;
}

static inline
int flash_img_buffered_write(struct flash_img_context *ctx, const uint8_t *data,
			     size_t len, bool flush)
{
	return 0;
}

/** Boot upgrade request modes */
#define BOOT_UPGRADE_TEST       0
#define BOOT_UPGRADE_PERMANENT  1

static inline int boot_request_upgrade(int permanent)
{
	return 0;
}

static inline int boot_write_img_confirmed(void)
{
	return 0;
}

static char current_version_str[sizeof("255.255.65535")] = "1.0.0";

#endif /* CONFIG_BOOTLOADER_MCUBOOT */

#endif /* __APP_FLASH_H__ */
