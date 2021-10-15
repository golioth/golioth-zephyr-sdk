/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_

#include <net/golioth.h>

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
 * @brief Represents incoming firmware from Golioth.
 */
struct golioth_fw_download_ctx {
	struct golioth_blockwise_download_ctx blockwise_ctx;
	char uri[64];
	size_t uri_len;
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
 * @param reply CoAP reply handler object used for notifying about received
 *              desired firmware description
 * @param desired_cb Callback that will be executed when desired firmware
 *                   description is received
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_observe_desired(struct golioth_client *client,
			       struct coap_reply *reply,
			       coap_reply_t desired_cb);

/**
 * @brief Request firmware download from Golioth
 *
 * @param client Client instance
 * @param ctx Firmware download context
 * @param uri Pointer to URI string
 * @param uri_len Length of URI string
 * @param reply CoAP reply handler object used for notifying about received
 *              firmware blocks
 * @param received_cb Callback that will be executed with each incoming block of
 *                    firmware
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_download(struct golioth_client *client,
			struct golioth_fw_download_ctx *ctx,
			const char *uri, size_t uri_len,
			struct coap_reply *reply,
			golioth_blockwise_download_received_t received_cb);

/**
 * @brief Report state of firmware
 *
 * @param client Client instance
 * @param package_name Package name of firmware
 * @param state State of firmware
 * @param result Result of downloading or updating firmware
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_report_state(struct golioth_client *client,
			    const char *package_name,
			    enum golioth_fw_state state,
			    enum golioth_dfu_result result);

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_ */
