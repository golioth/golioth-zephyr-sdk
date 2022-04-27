/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <util/mcumgr_util.h>
#include <settings/settings.h>

#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <mgmt/mcumgr/buf.h>

#include "cfg_mgmt/cfg_mgmt.h"
#include "mgmt/mgmt.h"

#include "zcbor_bulk/zcbor_bulk_priv.h"

static int cfg_mgmt_val_get(struct mgmt_ctxt *ctxt);
static int cfg_mgmt_val_set(struct mgmt_ctxt *ctxt);

static const struct mgmt_handler cfg_mgmt_group_handlers[] = {
	[CFG_MGMT_ID_VAL] = {
		IS_ENABLED(CONFIG_SETTINGS_RUNTIME) ? cfg_mgmt_val_get : NULL,
		cfg_mgmt_val_set,
	},
};

static struct mgmt_group cfg_mgmt_group = {
	.mg_handlers = cfg_mgmt_group_handlers,
	.mg_handlers_count = ARRAY_SIZE(cfg_mgmt_group_handlers),
	.mg_group_id = MGMT_GROUP_ID_CONFIG,
};

static int cfg_mgmt_impl_get(const char *name, uint8_t *val, size_t *val_len)
{
#ifdef CONFIG_SETTINGS_RUNTIME
	int ret;

	ret = settings_runtime_get(name, val, *val_len);
	if (ret < 0) {
		if (ret == -ENOENT) {
			return MGMT_ERR_ENOENT;
		}

		return MGMT_ERR_EUNKNOWN;
	}

	*val_len = ret;

	return 0;
#else
	return MGMT_ERR_ENOTSUP;
#endif
}

/**
 * Command handler: cfg val
 */
static int cfg_mgmt_val_get(struct mgmt_ctxt *ctxt)
{
	struct zcbor_string map_name = {};
	char name[CONFIG_MCUMGR_CMD_CFG_MGMT_KEY_MAX_LEN];
	char val[CONFIG_MCUMGR_CMD_CFG_MGMT_VAL_MAX_LEN];
	size_t val_len = sizeof(val);
	zcbor_state_t *zsd = ctxt->cnbd->zs;
	zcbor_state_t *zse = ctxt->cnbe->zs;
	bool ok;
	int rc;

	if (!zcbor_map_start_decode(zsd)) {
		return MGMT_ERR_EUNKNOWN;
	}

	do {
		struct zcbor_string map_key;

		ok = zcbor_tstr_decode(zsd, &map_key);

		if (ok) {
			static const char name_key[] = "name";

			if (map_key.len == ARRAY_SIZE(name_key) - 1 &&
			    memcmp(map_key.value, name_key, ARRAY_SIZE(name_key) - 1) == 0) {
				ok = zcbor_tstr_decode(zsd, &map_name);
				break;
			}

			ok = zcbor_any_skip(zsd, NULL);
		}
	} while (ok);

	if (!ok || !zcbor_map_end_decode(zsd)) {
		return MGMT_ERR_EUNKNOWN;
	}

	if (map_name.len == 0 || map_name.len >= sizeof(name)) {
		return MGMT_ERR_EINVAL;
	}

	/* Copy to local buffer to add NULL termination */
	memcpy(name, map_name.value, map_name.len);
	name[map_name.len] = '\0';

	rc = cfg_mgmt_impl_get(name, val, &val_len);

	ok = zcbor_tstr_put_lit(zse, "rc") &&
		zcbor_int32_put(zse, rc);

	if (ok && rc == 0) {
		ok = zcbor_tstr_put_lit(zse, "val") &&
			zcbor_bstr_encode_ptr(zse, val, val_len);
	}

	return ok ? MGMT_ERR_EOK : MGMT_ERR_ENOMEM;
}

static int cfg_mgmt_impl_set(const char *name, const uint8_t *val,
			     size_t val_len)
{
	int err;

#ifdef CONFIG_SETTINGS_RUNTIME
	err = settings_runtime_set(name, val, val_len);
	if (err) {
		return MGMT_ERR_EUNKNOWN;
	}
#endif

	err = settings_save_one(name, val, val_len);
	if (err) {
		return MGMT_ERR_EUNKNOWN;
	}

	return 0;
}

static bool zcbor_bstr_or_tstr_decode(zcbor_state_t *state, struct zcbor_string *result)
{
	bool ok;

	ok = zcbor_bstr_decode(state, result);
	if (ok) {
		return ok;
	}

	return zcbor_tstr_decode(state, result);
}

/**
 * Command handler: cfg val
 */
static int cfg_mgmt_val_set(struct mgmt_ctxt *ctxt)
{
	struct zcbor_string map_name = {};
	char name[CONFIG_MCUMGR_CMD_CFG_MGMT_KEY_MAX_LEN];
	struct zcbor_string val;
	zcbor_state_t *zsd = ctxt->cnbd->zs;
	zcbor_state_t *zse = ctxt->cnbe->zs;
	size_t decoded;
	bool ok;
	bool save;
	int rc;

	struct zcbor_map_decode_key_val cfg_set_decode[] = {
		ZCBOR_MAP_DECODE_KEY_VAL(name, zcbor_tstr_decode, &map_name),
		ZCBOR_MAP_DECODE_KEY_VAL(val, zcbor_bstr_or_tstr_decode, &val),
		ZCBOR_MAP_DECODE_KEY_VAL(save, zcbor_bool_decode, &save),
	};

	ok = zcbor_map_decode_bulk(zsd, cfg_set_decode,
		ARRAY_SIZE(cfg_set_decode), &decoded) == 0;

	if (!ok) {
		return MGMT_ERR_EINVAL;
	}

	if (map_name.len == 0 || map_name.len >= sizeof(name)) {
		return MGMT_ERR_EINVAL;
	}

	/* Copy to local buffer to add NULL termination */
	memcpy(name, map_name.value, map_name.len);
	name[map_name.len] = '\0';

	rc = cfg_mgmt_impl_set(name, val.value, val.len);

	ok = zcbor_tstr_put_lit(zse, "rc") &&
		zcbor_int32_put(zse, rc);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_ENOMEM;
}

void cfg_mgmt_register_group(void)
{
	mgmt_register_group(&cfg_mgmt_group);
}
