/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <qcbor/qcbor.h>
#include <qcbor/qcbor_spiffy_decode.h>
#include <qcbor/posix_error_map.h>

#include <net/golioth.h>
#include "lightdb_helpers.h"

int golioth_lightdb_get_basic(struct golioth_client *client, const uint8_t *path,
			      void *value_void,
			      void (*get)(QCBORDecodeContext *dec, void *value_void))
{
	QCBORDecodeContext dec;
	uint8_t buf_raw[16];
	UsefulBufC buf = { buf_raw, ARRAY_SIZE(buf_raw) };
	QCBORError qerr;
	int err;

	err = golioth_lightdb_get(client, path, GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				  buf_raw, &buf.len);
	if (err) {
		return err;
	}

	QCBORDecode_Init(&dec, buf, QCBOR_DECODE_MODE_NORMAL);

	get(&dec, value_void);

	qerr = QCBORDecode_Finish(&dec);
	if (qerr != QCBOR_SUCCESS) {
		return qcbor_error_to_posix(qerr);
	}

	return 0;
}

#define LIGHTDB_GET_SIMPLE_DEFINE(_type, _get)				\
void golioth_lightdb_qcbor_get_##_type(QCBORDecodeContext *dec,		\
				       void *value_void)		\
{									\
	_type *value = value_void;					\
									\
	_get(dec, value);						\
}

LIGHTDB_GET_SIMPLE_DEFINE(bool, QCBORDecode_GetBool);
LIGHTDB_GET_SIMPLE_DEFINE(int64_t, QCBORDecode_GetInt64);
LIGHTDB_GET_SIMPLE_DEFINE(uint64_t, QCBORDecode_GetUInt64);
LIGHTDB_GET_SIMPLE_DEFINE(double, QCBORDecode_GetDouble);

int golioth_lightdb_get_int64_t_clamp(struct golioth_client *client, const uint8_t *path,
				      int64_t *value, int64_t min, int64_t max)
{
	int err;

	err = golioth_lightdb_get_basic(client, path, value,
					golioth_lightdb_qcbor_get_int64_t);
	if (err) {
		return err;
	}

	if (*value > max) {
		*value = max;
		return -ERANGE;
	}

	if (*value < min) {
		*value = min;
		return -ERANGE;
	}

	return 0;
}

int golioth_lightdb_get_uint64_t_clamp(struct golioth_client *client, const uint8_t *path,
				       uint64_t *value, uint64_t max)
{
	int err;

	err = golioth_lightdb_get_basic(client, path, value,
					golioth_lightdb_qcbor_get_uint64_t);
	if (err) {
		return err;
	}

	if (*value > max) {
		*value = max;
		return -ERANGE;
	}

	return 0;
}

int golioth_lightdb_set_basic(struct golioth_client *client, const uint8_t *path,
			      const void *value_void,
			      void (*add)(QCBOREncodeContext *enc,
					  const void *value_void))
{
	QCBOREncodeContext enc;
	UsefulBuf_MAKE_STACK_UB(buf, 16);
	QCBORError qerr;
	size_t encoded_len;

	QCBOREncode_Init(&enc, buf);

	add(&enc, value_void);

	qerr = QCBOREncode_FinishGetSize(&enc, &encoded_len);
	if (qerr != QCBOR_SUCCESS) {
		return qcbor_error_to_posix(qerr);
	}

	return golioth_lightdb_set(client, path, GOLIOTH_CONTENT_FORMAT_APP_CBOR,
				   buf.ptr, encoded_len);
}

#define LIGHTDB_SET_SIMPLE_DEFINE(_type, _encode)			\
void golioth_lightdb_qcbor_add_##_type(QCBOREncodeContext *enc,		\
			       const void *value_void)			\
{									\
	const _type *value = value_void;				\
									\
	_encode(enc, *value);						\
}

LIGHTDB_SET_SIMPLE_DEFINE(bool, QCBOREncode_AddBool);
LIGHTDB_SET_SIMPLE_DEFINE(int64_t, QCBOREncode_AddInt64);
LIGHTDB_SET_SIMPLE_DEFINE(uint64_t, QCBOREncode_AddUInt64);
LIGHTDB_SET_SIMPLE_DEFINE(float, QCBOREncode_AddFloat);
LIGHTDB_SET_SIMPLE_DEFINE(double, QCBOREncode_AddDouble);

void golioth_lightdb_qcbor_add_stringz(QCBOREncodeContext *enc,
				       const void *value_void)
{
	const char *value = value_void;

	QCBOREncode_AddSZString(enc, value);
}
