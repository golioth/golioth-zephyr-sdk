/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <init.h>
#include <settings/settings.h>

static int settings_autoload(const struct device *dev)
{
	return settings_load();
}

SYS_INIT(settings_autoload, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
