/*
 * Copyright (c) 2021-2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <net/golioth/fw.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "pathv.h"
#include "zcbor_utils.h"

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

struct component_tstr_value {
	uint8_t *value;
	size_t *value_len;
};

struct component_info {
	struct component_tstr_value version;
	struct component_tstr_value uri;
};

static int component_entry_decode_value(zcbor_state_t *zsd, void *void_value)
{
	struct component_tstr_value *value = void_value;
	struct zcbor_string tstr;
	bool ok;

	ok = zcbor_tstr_decode(zsd, &tstr);
	if (!ok) {
		return -EBADMSG;
	}

	if (tstr.len > *value->value_len) {
		LOG_ERR("Not enough space to store");
		return -ENOMEM;
	}

	memcpy(value->value, tstr.value, tstr.len);
	*value->value_len = tstr.len;

	return 0;
}

static int components_decode(zcbor_state_t *zsd, void *value)
{
	struct component_info *component = value;
	struct zcbor_map_entry map_entries[] = {
		ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_VERSION,
				    component_entry_decode_value,
				    &component->version),
		ZCBOR_U32_MAP_ENTRY(COMPONENT_KEY_URI,
				    component_entry_decode_value,
				    &component->uri),
	};
	int err;
	bool ok;

	ok = zcbor_list_start_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not start CBOR list correctly");
		return -EBADMSG;
	}

	err = zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
	if (err) {
		return err;
	}

	ok = zcbor_list_end_decode(zsd);
	if (!ok) {
		LOG_WRN("Did not end CBOR list correctly");
		return -EBADMSG;
	}

	return err;
}

int golioth_fw_desired_parse(const uint8_t *payload, uint16_t payload_len,
			     uint8_t *version, size_t *version_len,
			     uint8_t *uri, size_t *uri_len)
{
	ZCBOR_STATE_D(zsd, 3, payload, payload_len, 1);
	int64_t manifest_sequence_number;
	struct component_info component_info = {
		.version = {version, version_len},
		.uri = {uri, uri_len},
	};
	struct zcbor_map_entry map_entries[] = {
		ZCBOR_U32_MAP_ENTRY(MANIFEST_KEY_SEQUENCE_NUMBER,
				    zcbor_map_int64_decode,
				    &manifest_sequence_number),
		ZCBOR_U32_MAP_ENTRY(MANIFEST_KEY_COMPONENTS,
				    components_decode,
				    &component_info),
	};
	int err;

	err = zcbor_map_decode(zsd, map_entries, ARRAY_SIZE(map_entries));
	if (err) {
		if (err != -ENOENT) {
			LOG_WRN("Failed to decode desired map");
		}

		return err;
	}

	return 0;
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
		return err;
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

static int fw_report_state_encode(zcbor_state_t *zse,
				  const char *current_version,
				  const char *target_version,
				  enum golioth_fw_state state,
				  enum golioth_dfu_result result)
{
	bool ok;

	ok = zcbor_map_start_encode(zse, 1);
	if (!ok) {
		LOG_WRN("Did not start CBOR map correctly");
		return -ENOMEM;
	}

	ok = zcbor_tstr_put_lit(zse, "s") &&
	     zcbor_uint32_put(zse, state);
	if (!ok) {
		return -ENOMEM;
	}

	ok = zcbor_tstr_put_lit(zse, "r") &&
	     zcbor_uint32_put(zse, result);
	if (!ok) {
		return -ENOMEM;
	}

	if (current_version && current_version[0] != '\0') {
		ok = zcbor_tstr_put_lit(zse, "v") &&
		     zcbor_tstr_put_term(zse, current_version);
		if (!ok) {
			return -ENOMEM;
		}
	}

	if (target_version && target_version[0] != '\0') {
		ok = zcbor_tstr_put_lit(zse, "t") &&
		     zcbor_tstr_put_term(zse, target_version);
		if (!ok) {
			return -ENOMEM;
		}
	}

	ok = zcbor_map_end_encode(zse, 1);
	if (!ok) {
		LOG_WRN("Did not end CBOR map correctly");
		return -ENOMEM;
	}

	return 0;
}

int golioth_fw_report_state_cb(struct golioth_client *client,
			       const char *package_name,
			       const char *current_version,
			       const char *target_version,
			       enum golioth_fw_state state,
			       enum golioth_dfu_result result,
			       golioth_req_cb_t cb, void *user_data)
{
	uint8_t encode_buf[64];
	ZCBOR_STATE_E(zse, 1, encode_buf, sizeof(encode_buf), 1);
	int err;

	err = fw_report_state_encode(zse, current_version, target_version,
				     state, result);
	if (err) {
		return err;
	}

	return golioth_coap_req_cb(client, COAP_METHOD_POST,
				     PATHV(GOLIOTH_FW_REPORT_STATE, package_name),
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     encode_buf, zse->payload - encode_buf,
				     cb, user_data,
				     0);
}

int golioth_fw_report_state(struct golioth_client *client,
			    const char *package_name,
			    const char *current_version,
			    const char *target_version,
			    enum golioth_fw_state state,
			    enum golioth_dfu_result result)
{
	uint8_t encode_buf[64];
	ZCBOR_STATE_E(zse, 1, encode_buf, sizeof(encode_buf), 1);
	int err;

	err = fw_report_state_encode(zse, current_version, target_version,
				     state, result);
	if (err) {
		return err;
	}

	return golioth_coap_req_sync(client, COAP_METHOD_POST,
				     PATHV(GOLIOTH_FW_REPORT_STATE, package_name),
				     GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				     encode_buf, zse->payload - encode_buf,
				     NULL, NULL,
				     0);
}
