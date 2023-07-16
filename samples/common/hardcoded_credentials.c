/*
 * Copyright (c) 2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_hardcoded_credentials, LOG_LEVEL_DBG);

#include <zephyr/init.h>
#include <zephyr/net/tls_credentials.h>

static const uint8_t tls_client_crt[] = {
#if defined(CONFIG_GOLIOTH_SAMPLE_HARDCODED_CRT_PATH)
#include "golioth-systemclient-crt.inc"
#endif
};

static const uint8_t tls_client_key[] = {
#if defined(CONFIG_GOLIOTH_SAMPLE_HARDCODED_KEY_PATH)
#include "golioth-systemclient-key.inc"
#endif
};

#if defined(CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK_ID)
#define HARDCODED_PSK_ID CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK_ID
#else
#define HARDCODED_PSK_ID ""
#endif

#if defined(CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK)
#define HARDCODED_PSK CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK
#else
#define HARDCODED_PSK ""
#endif


#if defined(CONFIG_MBEDTLS_PSK_MAX_LEN) && defined(CONFIG_GOLIOTH_AUTH_METHOD_PSK)
BUILD_ASSERT(sizeof(CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK) - 1 <= CONFIG_MBEDTLS_PSK_MAX_LEN,
	     "PSK exceeds mbedTLS configured maximum PSK length");
#endif

static int hardcoded_credentials_init(void)
{
	if (IS_ENABLED(CONFIG_GOLIOTH_AUTH_METHOD_CERT)) {
		int err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
					 TLS_CREDENTIAL_SERVER_CERTIFICATE,
					 tls_client_crt, ARRAY_SIZE(tls_client_crt));
		if (err < 0) {
			LOG_ERR("Failed to register server cert: %d", err);
		}

		err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
					 TLS_CREDENTIAL_PRIVATE_KEY,
					 tls_client_key, ARRAY_SIZE(tls_client_key));
		if (err < 0) {
			LOG_ERR("Failed to register private key: %d", err);
		}
	} else if (IS_ENABLED(CONFIG_GOLIOTH_AUTH_METHOD_PSK)) {
		int err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
					 TLS_CREDENTIAL_PSK,
					 HARDCODED_PSK, sizeof(HARDCODED_PSK) - 1);
		if (err < 0) {
			LOG_ERR("Failed to register PSK: %d", err);
		}

		err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
					 TLS_CREDENTIAL_PSK_ID,
					 HARDCODED_PSK_ID, sizeof(HARDCODED_PSK_ID) - 1);
		if (err < 0) {
			LOG_ERR("Failed to register PSK ID: %d", err);
		}

	}

	return 0;
}

SYS_INIT(hardcoded_credentials_init, APPLICATION, CONFIG_GOLIOTH_SYSTEM_CLIENT_INIT_PRIORITY);
