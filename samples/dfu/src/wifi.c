/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_wifi, LOG_LEVEL_DBG);

#include <net/wifi_mgmt.h>

#include "wifi.h"

struct wifi_data {
	struct net_mgmt_event_callback wifi_mgmt_cb;
	struct k_sem connect_sem;
	int connect_status;
};

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	struct wifi_data *wifi = CONTAINER_OF(cb, struct wifi_data, wifi_mgmt_cb);
	const struct wifi_status *status = cb->info;

	LOG_DBG("wifi event: %x", (unsigned int) mgmt_event);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT:
		wifi->connect_status = status->status;
		k_sem_give(&wifi->connect_sem);
		break;
	default:
		break;
	}
}

void wifi_connect(void)
{
	struct wifi_connect_req_params params = {
		.ssid = CONFIG_GOLIOTH_SAMPLE_WIFI_SSID,
		.ssid_length = sizeof(CONFIG_GOLIOTH_SAMPLE_WIFI_SSID) - 1,
		.psk = CONFIG_GOLIOTH_SAMPLE_WIFI_PSK,
		.psk_length = sizeof(CONFIG_GOLIOTH_SAMPLE_WIFI_PSK) - 1,
		.channel = WIFI_CHANNEL_ANY,
		.security = sizeof(CONFIG_GOLIOTH_SAMPLE_WIFI_PSK) - 1 > 0 ?
			WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
	};
	struct wifi_data wifi;
	int err;
	int attempts = 10;

	k_sem_init(&wifi.connect_sem, 0, 1);

	net_mgmt_init_event_callback(&wifi.wifi_mgmt_cb,
				     wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi.wifi_mgmt_cb);

	while (attempts--) {
		err = net_mgmt(NET_REQUEST_WIFI_CONNECT, net_if_get_default(),
			       &params, sizeof(params));
		if (err == -EALREADY) {
			LOG_INF("Already connected to WiFi");
			break;
		} else if (err) {
			LOG_ERR("Failed to request WiFi connect: %d", err);
			goto retry;
		}

		/* wait for notification from connect request */
		err = k_sem_take(&wifi.connect_sem, K_SECONDS(20));
		if (err) {
			LOG_ERR("Timeout waiting for connection with WiFi");
			goto retry;
		}

		/* verify connect status */
		if (wifi.connect_status == 0) {
			LOG_INF("Successfully connected to WiFi");
			break;
		} else {
			LOG_ERR("Failed to connect to WiFi: %d",
				wifi.connect_status);
			goto retry;
		}

	retry:
		k_sleep(K_SECONDS(5));
	}

	net_mgmt_del_event_callback(&wifi.wifi_mgmt_cb);
}
