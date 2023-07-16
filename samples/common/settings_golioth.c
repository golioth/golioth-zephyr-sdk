/*
 * Copyright (c) 2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_sample_settings, CONFIG_GOLIOTH_SYSTEM_CLIENT_LOG_LEVEL);

#include <errno.h>
#include <net/golioth/system_client.h>
#include <zephyr/init.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/settings/settings.h>

/*
 * TLS credentials subsystem just remembers pointers to memory areas where
 * credentials are stored. This means that we need to allocate memory for
 * credentials ourselves.
 */
static uint8_t golioth_dtls_psk[CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_MAX_LEN + 1];
static size_t golioth_dtls_psk_len;
static uint8_t golioth_dtls_psk_id[CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID_MAX_LEN + 1];
static size_t golioth_dtls_psk_id_len;

static int golioth_settings_get(const char *name, char *dst, int val_len_max)
{
	uint8_t *val;
	size_t val_len;

	if (!strcmp(name, "psk")) {
		val = golioth_dtls_psk;
		val_len = strlen(golioth_dtls_psk);
	} else if (!strcmp(name, "psk-id")) {
		val = golioth_dtls_psk_id;
		val_len = strlen(golioth_dtls_psk_id);
	} else {
		LOG_WRN("Unsupported key '%s'", name);
		return -ENOENT;
	}

	if (val_len > val_len_max) {
		LOG_ERR("Not enough space (%zu %d)", val_len, val_len_max);
		return -ENOMEM;
	}

	memcpy(dst, val, val_len);

	return val_len;
}

static int golioth_settings_set(const char *name, size_t len_rd,
				settings_read_cb read_cb, void *cb_arg)
{
	enum tls_credential_type type;
	uint8_t *value;
	size_t *value_len;
	size_t buffer_len;
	ssize_t ret;
	int err;

	if (!strcmp(name, "psk")) {
		type = TLS_CREDENTIAL_PSK;
		value = golioth_dtls_psk;
		value_len = &golioth_dtls_psk_len;
		buffer_len = sizeof(golioth_dtls_psk);
	} else if (!strcmp(name, "psk-id")) {
		type = TLS_CREDENTIAL_PSK_ID;
		value = golioth_dtls_psk_id;
		value_len = &golioth_dtls_psk_id_len;
		buffer_len = sizeof(golioth_dtls_psk_id);
	} else {
		LOG_ERR("Unsupported key '%s'", name);
		return -ENOTSUP;
	}

	if (IS_ENABLED(CONFIG_SETTINGS_RUNTIME)) {
		err = tls_credential_delete(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG, type);
		if (err && err != -ENOENT) {
			LOG_ERR("Failed to delete cred %s: %d",
				name, err);
			return err;
		}
	}

	ret = read_cb(cb_arg, value, buffer_len);
	if (ret < 0) {
		LOG_ERR("Failed to read value: %d", (int) ret);
		return ret;
	}

	if (ret >= buffer_len) {
		LOG_ERR("Configured %s does not fit into (%zu bytes) static buffer!",
			name, buffer_len - 1);
		return -ENOMEM;
	}

	*value_len = ret;

	LOG_DBG("Name: %s", name);
	LOG_HEXDUMP_DBG(value, *value_len, "value");

	err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG, type,
				 value, *value_len);
	if (err) {
		LOG_ERR("Failed to add cred %s: %d", name, err);
		return err;
	}

	golioth_system_client_request_reconnect();

	return 0;
}

static int golioth_settings_init(void)
{
	int err = settings_subsys_init();

	if (err) {
		LOG_ERR("Failed to initialize settings subsystem: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(golioth_settings_init, APPLICATION,
	 CONFIG_GOLIOTH_SYSTEM_CLIENT_INIT_PRIORITY);

SETTINGS_STATIC_HANDLER_DEFINE(golioth, "golioth",
	IS_ENABLED(CONFIG_SETTINGS_RUNTIME) ? golioth_settings_get : NULL,
	golioth_settings_set, NULL, NULL);
