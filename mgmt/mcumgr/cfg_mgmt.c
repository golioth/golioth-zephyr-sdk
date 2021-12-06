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

#include "cborattr/cborattr.h"
#include "cfg_mgmt/cfg_mgmt.h"
#include "mgmt/mgmt.h"
#include "tinycbor/cbor.h"

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
	char name[CONFIG_MCUMGR_CMD_CFG_MGMT_KEY_MAX_LEN];
	char val[CONFIG_MCUMGR_CMD_CFG_MGMT_VAL_MAX_LEN];
	size_t val_len = sizeof(val);
	CborError err = 0;
	int rc;

	const struct cbor_attr_t attrs[] = {
		{
			.attribute = "name",
			.type = CborAttrTextStringType,
			.addr.string = name,
			.nodefault = true,
			.len = sizeof(name),
		},
		{ },
	};

	name[0] = '\0';

	rc = cbor_read_object(&ctxt->it, attrs);
	if (rc != 0) {
		return MGMT_ERR_EINVAL;
	}

	rc = cfg_mgmt_impl_get(name, val, &val_len);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "rc");
	err |= cbor_encode_int(&ctxt->encoder, rc);

	if (rc == 0) {
		err |= cbor_encode_text_stringz(&ctxt->encoder, "val");
		err |= cbor_encode_byte_string(&ctxt->encoder, val, val_len);
	}

	if (err != 0) {
		return MGMT_ERR_ENOMEM;
	}

	return 0;
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

/**
 * Command handler: cfg val
 */
static int cfg_mgmt_val_set(struct mgmt_ctxt *ctxt)
{
	char name[CONFIG_MCUMGR_CMD_CFG_MGMT_KEY_MAX_LEN];
	char val[CONFIG_MCUMGR_CMD_CFG_MGMT_VAL_MAX_LEN];
	size_t val_len = SIZE_MAX;
	bool save;
	CborError err = 0;
	int rc;

	const struct cbor_attr_t attrs[] = {
		{
			.attribute = "name",
			.type = CborAttrTextStringType,
			.addr.string = name,
			.nodefault = true,
			.len = sizeof(name),
		},
		{
			.attribute = "val",
			.type = CborAttrByteStringType,
			.addr.bytestring.data = val,
			.addr.bytestring.len = &val_len,
			.nodefault = true,
			.len = sizeof(val),
		},
		{
			.attribute = "val",
			.type = CborAttrTextStringType,
			.addr.string = val,
			.nodefault = true,
			.len = sizeof(val),
		},
		{
			.attribute = "save",
			.type = CborAttrBooleanType,
			.addr.boolean = &save,
			.dflt.boolean = false,
		},
		{ },
	};

	name[0] = '\0';
	val[0] = '\0';

	rc = cbor_read_object(&ctxt->it, attrs);
	if (rc != 0) {
		return MGMT_ERR_EINVAL;
	}

	if (val_len == SIZE_MAX) {
		val_len = strlen(val);
	}

	if (name[0] == '\0') {
		return MGMT_ERR_EINVAL;
	}

	rc = cfg_mgmt_impl_set(name, val, val_len);

	err |= cbor_encode_text_stringz(&ctxt->encoder, "rc");
	err |= cbor_encode_int(&ctxt->encoder, rc);

	if (err != 0) {
		return MGMT_ERR_ENOMEM;
	}

	return 0;
}

void cfg_mgmt_register_group(void)
{
	mgmt_register_group(&cfg_mgmt_group);
}
