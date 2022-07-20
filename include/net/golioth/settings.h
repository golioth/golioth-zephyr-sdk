/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_SETTINGS_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_SETTINGS_H_

#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <net/coap.h>
#include <stdint.h>

/**
 * @defgroup golioth_settings Golioth Settings
 * @ingroup net
 * Functions for interacting with the Golioth Settings service
 *
 * The Settings service is for long-lived persistent configuration data.
 * Settings are configured/written from the cloud and read
 * by the device. The device observes
 * for settings updates, and reports status of applying the
 * settings to the cloud.
 *
 * Each setting is a key/value pair, where the key is a string
 * and the value can be bool, float, or string.
 *
 * Overall, the flow is:
 *
 * 1. Application registers a callback to handle settings.
 * 2. This library observes for settings changes from cloud.
 * 3. Cloud pushes settings changes to device.
 * 4. For each setting, this library calls the user-registered callback.
 * 5. This library reports status of applying settings to cloud.
 *
 * This library is responsible for interfacing with the cloud
 * and has no knowledge of the specific settings.
 * @{
 */

struct golioth_client;

/**
 * @brief Enumeration of Settings status codes
 */
enum golioth_settings_status {
	/* Setting applied successfully to the device */
	GOLIOTH_SETTINGS_SUCCESS = 0,
	/* The setting key is not recognized, this setting is unknown */
	GOLIOTH_SETTINGS_KEY_NOT_RECOGNIZED = 1,
	/* The setting key is too long, ill-formatted */
	GOLIOTH_SETTINGS_KEY_NOT_VALID = 2,
	/* The setting value is improperly formatted */
	GOLIOTH_SETTINGS_VALUE_FORMAT_NOT_VALID = 3,
	/* The setting value is outside of allowed range */
	GOLIOTH_SETTINGS_VALUE_OUTSIDE_RANGE = 4,
	/* The setting value string is too long, exceeds max length */
	GOLIOTH_SETTINGS_VALUE_STRING_TOO_LONG = 5,
	/* Other general error */
	GOLIOTH_SETTINGS_GENERAL_ERROR = 6,
};

/**
 * @brief Different types of setting values
 *
 * Note that there is no "int" type because all numbers sent by
 * the server are formatted as floating point.
 */
enum golioth_settings_value_type {
	GOLIOTH_SETTINGS_VALUE_TYPE_UNKNOWN,
	GOLIOTH_SETTINGS_VALUE_TYPE_BOOL,
	GOLIOTH_SETTINGS_VALUE_TYPE_FLOAT,
	GOLIOTH_SETTINGS_VALUE_TYPE_STRING,
};

/**
 * @brief A setting value.
 *
 * The type will dictate which of the union fields to use.
 */
struct golioth_settings_value {
	enum golioth_settings_value_type type;
	union {
		bool b;
		float f;
		struct {
			const char *ptr; /* not NULL-terminated */
			size_t len;
		} string;
	};
};

/**
 * @brief Callback for an individual setting
 *
 * @param key The setting key, NULL-terminated
 * @param value The setting value
 *
 * @return GOLIOTH_SETTINGS_SUCCESS - setting is valid
 * @return Otherwise - setting is not valid
 */
typedef enum golioth_settings_status (*golioth_settings_cb)(
		const char *key,
		const struct golioth_settings_value *value);

/**
 * @brief Settings state data, placed in @ref struct golioth_client
 */
struct golioth_settings {
#if defined(CONFIG_GOLIOTH_SETTINGS)
	bool initialized;
	golioth_settings_cb callback;
	struct coap_reply observe_reply;
#endif
};

/**
 * @brief Register callback for handling settings.
 *
 * The client will be used to observe for settings from the cloud.
 *
 * @param client Client handle
 * @param callback Callback function to call for settings changes
 *
 * @return 0 - Callback registered successfully
 * @return <0 - Error, callback not registered
 */
int golioth_settings_register_callback(struct golioth_client *client,
				       golioth_settings_cb callback);

/** @} */

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_SETTINGS_H_ */
