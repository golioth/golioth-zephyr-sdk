/*
 * Copyright (c) 2021 Marcin Niestroj
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <init.h>

#ifdef CONFIG_MCUMGR_CMD_CFG_MGMT
#include "cfg_mgmt/cfg_mgmt.h"
#else
static inline void cfg_mgmt_register_group(void) {}
#endif

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include "fs_mgmt/fs_mgmt.h"
#else
static inline void fs_mgmt_register_group(void) {}
#endif

#ifdef CONFIG_MCUMGR_CMD_IMG_MGMT
#include "img_mgmt/img_mgmt.h"
#else
static inline void img_mgmt_register_group(void) {}
#endif

#ifdef CONFIG_MCUMGR_CMD_LOG_MGMT
#include "log_mgmt/log_mgmt.h"
#else
static inline void log_mgmt_register_group(void) {}
#endif

#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
#include "os_mgmt/os_mgmt.h"
#else
static inline void os_mgmt_register_group(void) {}
#endif

#ifdef CONFIG_MCUMGR_CMD_STAT_MGMT
#include "stat_mgmt/stat_mgmt.h"
#else
static inline void stat_mgmt_register_group(void) {}
#endif

static int mcumgr_init(const struct device *dev)
{
	cfg_mgmt_register_group();
	fs_mgmt_register_group();
	img_mgmt_register_group();
	log_mgmt_register_group();
	os_mgmt_register_group();
	stat_mgmt_register_group();

	return 0;
}

SYS_INIT(mcumgr_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
