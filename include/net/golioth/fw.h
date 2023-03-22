/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_

#include <stddef.h>
#include <stdint.h>
#include <net/golioth/req.h>

struct golioth_client;

/**
 * @brief State of downloading or updating the firmware.
 */
enum golioth_fw_state {
	GOLIOTH_FW_STATE_IDLE = 0,
	GOLIOTH_FW_STATE_DOWNLOADING = 1,
	GOLIOTH_FW_STATE_DOWNLOADED = 2,
	GOLIOTH_FW_STATE_UPDATING = 3,
};

/**
 * @brief Result of downloading or updating the firmware.
 */
enum golioth_dfu_result {
	GOLIOTH_DFU_RESULT_INITIAL = 0,
	GOLIOTH_DFU_RESULT_FIRMWARE_UPDATED_SUCCESSFULLY,
	GOLIOTH_DFU_RESULT_NOT_ENOUGH_FLASH_MEMORY,
	GOLIOTH_DFU_RESULT_OUT_OF_RAM,
	GOLIOTH_DFU_RESULT_CONNECTION_LOST,
	GOLIOTH_DFU_RESULT_INTEGRITY_CHECK_FAILURE,
	GOLIOTH_DFU_RESULT_UNSUPPORTED_PACKAGE_TYPE,
	GOLIOTH_DFU_RESULT_INVALID_URI,
	GOLIOTH_DFU_RESULT_FIRMWARE_UPDATE_FAILED,
	GOLIOTH_DFU_RESULT_UNSUPPORTED_PROTOCOL,
};

/**
 * @brief Parse desired firmware description
 *
 * @param payload Pointer to CBOR encoded 'desired' description
 * @param payload_len Length of CBOR encoded 'desired' description
 * @param version Pointer to version string, which will be updated by this
 *                function
 * @param version_len On input pointer to available space in version string, on
 *                    output actual length of version string
 * @param uri URI of the image, which will be updated by this function
 * @param uri_len On input pointer to available space in URI string, on
 *                output actual length of URI string
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len,
			     uint8_t *uri, size_t *uri_len);

/**
 * @brief Observe desired firmware
 *
 * @param client Client instance
 * @param cb Callback executed on response received, timeout or error
 * @param user_data User data passed to @p cb
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_observe_desired(struct golioth_client *client,
			       golioth_req_cb_t cb, void *user_data);

/**
 * @brief Request firmware download from Golioth
 *
 * @param client Client instance
 * @param uri Pointer to URI string
 * @param uri_len Length of URI string
 * @param cb Callback executed on response received, timeout or error
 * @param user_data User data passed to @p cb with each invocation
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_download(struct golioth_client *client,
			const uint8_t *uri, size_t uri_len,
			golioth_req_cb_t cb, void *user_data);

/**
 * @brief Report state of firmware (callback based)
 *
 * Asynchronously push firmware state information to Golioth
 *
 * @warning Experimental API
 *
 * @param client Client instance
 * @param package_name Package name of firmware
 * @param current_version Current firmware version
 * @param target_version Target firmware version
 * @param state State of firmware
 * @param result Result of downloading or updating firmware
 * @param cb Callback executed on response received, timeout or error
 * @param user_data User data passed to @p cb
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_report_state_cb(struct golioth_client *client,
			       const char *package_name,
			       const char *current_version,
			       const char *target_version,
			       enum golioth_fw_state state,
			       enum golioth_dfu_result result,
			       golioth_req_cb_t cb,
			       void *user_data);

/**
 * @brief Report state of firmware
 *
 * @param client Client instance
 * @param package_name Package name of firmware
 * @param current_version Current firmware version
 * @param target_version Target firmware version
 * @param state State of firmware
 * @param result Result of downloading or updating firmware
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_report_state(struct golioth_client *client,
			    const char *package_name,
			    const char *current_version,
			    const char *target_version,
			    enum golioth_fw_state state,
			    enum golioth_dfu_result result);

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_ */
