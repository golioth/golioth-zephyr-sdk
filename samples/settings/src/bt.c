/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(bt);

#include <bluetooth/bluetooth.h>
#include <init.h>
#include <mgmt/mcumgr/smp_bt.h>

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static int bt_init(const struct device *dev)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_MCUMGR_SMP_BT)) {
		smp_bt_register();
	}

	LOG_INF("Bluetooth initialized");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return err;
	}

	LOG_INF("Advertising successfully started");

	return 0;
}

SYS_INIT(bt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
