/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <net/golioth/fw.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#define GOLIOTH_FW_DESIRED	".u/desired"

#define GOLIOTH_FW_REPORT_STATE	".u/c"

enum {
	MANIFEST_KEY_SEQUENCE_NUMBER = 1,
	MANIFEST_KEY_HASH = 2,
	MANIFEST_KEY_COMPONENTS = 3,
};

enum {
	COMPONENT_KEY_PACKAGE = 1,
	COMPONENT_KEY_VERSION = 2,
	COMPONENT_KEY_HASH = 3,
	COMPONENT_KEY_SIZE = 4,
	COMPONENT_KEY_URI = 5,
};

static int parse_component(QCBORDecodeContext *decode_ctx,
			   uint8_t *version, size_t *version_len,
			   uint8_t *uri, size_t *uri_len)
{
	QCBORError qerr;
	UsefulBufC version_bufc;
	UsefulBufC uri_bufc;
	int err = 0;

	QCBORDecode_EnterMap(decode_ctx, NULL);

	QCBORDecode_GetTextStringInMapN(decode_ctx, COMPONENT_KEY_VERSION,
					&version_bufc);

	QCBORDecode_GetTextStringInMapN(decode_ctx, COMPONENT_KEY_URI,
					&uri_bufc);

	qerr = QCBORDecode_GetError(decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Failed to get version and URI: %d (%s)", qerr, qcbor_err_to_str(qerr));
		err = qcbor_error_to_posix(qerr);
		goto exit_map;
	}

	if (version_bufc.len > *version_len) {
		err = -ENOMEM;
		goto exit_map;
	}

	memcpy(version, version_bufc.ptr, version_bufc.len);
	*version_len = version_bufc.len;

	if (uri_bufc.len > *uri_len) {
		err = -ENOMEM;
		goto exit_map;
	}

	memcpy(uri, uri_bufc.ptr, uri_bufc.len);
	*uri_len = uri_bufc.len;

exit_map:
	QCBORDecode_ExitMap(decode_ctx);

	return err;
}

int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len,
			     uint8_t *uri, size_t *uri_len)
{
	int64_t manifest_sequence_number;
	UsefulBufC payload_bufc = { payload, payload_len };
	QCBORDecodeContext decode_ctx;
	QCBORError qerr;
	int err = 0;

	QCBORDecode_Init(&decode_ctx, payload_bufc, QCBOR_DECODE_MODE_NORMAL);

	QCBORDecode_EnterMap(&decode_ctx, NULL);

	QCBORDecode_GetInt64InMapN(&decode_ctx, MANIFEST_KEY_SEQUENCE_NUMBER,
				   &manifest_sequence_number);

	qerr = QCBORDecode_GetError(&decode_ctx);
	switch (qerr) {
	case QCBOR_SUCCESS:
		break;
	case QCBOR_ERR_LABEL_NOT_FOUND:
		LOG_DBG("No sequence-number found in manifest");
		err = -ENOENT;
		goto exit_root_map;
	default:
		LOG_ERR("Failed to get manifest bstr: %d (%s)", qerr, qcbor_err_to_str(qerr));
		err = qcbor_error_to_posix(qerr);
		goto exit_root_map;
	}

	LOG_INF("Manifest sequence-number: %d", (int) manifest_sequence_number);

	QCBORDecode_EnterArrayFromMapN(&decode_ctx, MANIFEST_KEY_COMPONENTS);

	err = parse_component(&decode_ctx, version, version_len, uri, uri_len);

	QCBORDecode_ExitArray(&decode_ctx);

	if (err) {
		goto exit_root_map;
	}

exit_root_map:
	QCBORDecode_ExitMap(&decode_ctx);

	if (err) {
		return err;
	}

	qerr = QCBORDecode_Finish(&decode_ctx);
	if (qerr != QCBOR_SUCCESS) {
		LOG_ERR("Error at the end: %d (%s)", qerr, qcbor_err_to_str(qerr));
		return qcbor_error_to_posix(qerr);
	}

	return err;
}

int golioth_fw_observe_desired(struct golioth_client *client,
			       golioth_req_cb_t cb, void *user_data)
{
	return golioth_coap_req_cb(client, COAP_METHOD_GET, PATHV(GOLIOTH_FW_DESIRED),
				   GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   NULL, 0,
				   cb, user_data,
				   GOLIOTH_COAP_REQ_OBSERVE);
}

int golioth_fw_download(struct golioth_client *client,
			const uint8_t *uri, size_t uri_len,
			golioth_req_cb_t cb, void *user_data)
{
	struct golioth_coap_req *req;
	int err;

	err = golioth_coap_req_new(&req, client, COAP_METHOD_GET, COAP_TYPE_CON,
				   GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN,
				   cb, user_data);
	if (err) {
		LOG_ERR("Failed to initialize CoAP GET request: %d", err);
		goto free_req;
	}

	err = coap_packet_append_uri_path_from_string(&req->request, uri, uri_len);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		goto free_req;
	}

	return golioth_coap_req_schedule(req);

free_req:
	golioth_coap_req_free(req);

	return err;
}

int golioth_fw_report_state(struct golioth_client *client,
			    const char *package_name,
			    const char *current_version,
			    const char *target_version,
			    enum golioth_fw_state state,
			    enum golioth_dfu_result result)
{
	QCBOREncodeContext encode_ctx;
	UsefulBuf_MAKE_STACK_UB(encode_bufc, 64);
	size_t encoded_len;
	QCBORError qerr;

	QCBOREncode_Init(&encode_ctx, encode_bufc);

	QCBOREncode_OpenMap(&encode_ctx);

	QCBOREncode_AddUInt64ToMap(&encode_ctx, "s", state);
	QCBOREncode_AddUInt64ToMap(&encode_ctx, "r", result);

	if (current_version && current_version[0] != '\0') {
		QCBOREncode_AddSZStringToMap(&encode_ctx, "v", current_version);
	}

	if (target_version && target_version[0] != '\0') {
		QCBOREncode_AddSZStringToMap(&encode_ctx, "t", target_version);
	}

	QCBOREncode_CloseMap(&encode_ctx);

	qerr = QCBOREncode_FinishGetSize(&encode_ctx, &encoded_len);
	if (qerr != QCBOR_SUCCESS) {
		return qcbor_error_to_posix(qerr);
	}

	return golioth_coap_req_sync(client, COAP_METHOD_POST,
				     PATHV(GOLIOTH_FW_REPORT_STATE, package_name),
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     encode_bufc.ptr, encoded_len,
				     NULL, NULL,
				     0);
}
