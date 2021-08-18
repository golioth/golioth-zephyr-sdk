/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_

#include <net/golioth.h>

/**
 * @brief Represents incoming firmware from Golioth.
 */
struct golioth_fw_download_ctx {
	struct golioth_blockwise_download_ctx blockwise_ctx;
	char version_query[64];
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
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len);

/**
 * @brief Observe desired firmware
 *
 * @param client Client instance
 * @param current_version Currently running version (used in request for new
 *                        firmware)
 * @param reply CoAP reply handler object used for notifying about received
 *              desired firmware description
 * @param desired_cb Callback that will be executed when desired firmware
 *                   description is received
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int golioth_fw_observe_desired(struct golioth_client *client,
			       const char *current_version,
			       struct coap_reply *reply,
			       coap_reply_t desired_cb);

/**
 * @brief Request firmware download from Golioth
 *
 * @param client Client instance
 * @param ctx Firmware download context
 * @param version Pointer to version string
 * @param version_len Length of version string
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
			const char *version, size_t version_len,
			struct coap_reply *reply,
			golioth_blockwise_download_received_t received_cb);

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_FW_H_ */
