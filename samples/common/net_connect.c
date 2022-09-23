/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_samples, LOG_LEVEL_DBG);

#include <samples/common/net_connect.h>
#include <samples/common/wifi.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>

void net_connect(void)
{
	struct net_if *iface = net_if_get_default();

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
		LOG_INF("Connecting to WiFi");
		wifi_connect(iface);
	}

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_DHCP_START)) {
		LOG_INF("Starting DHCP to obtain IP address");
		net_dhcpv4_start(iface);
	}
}
