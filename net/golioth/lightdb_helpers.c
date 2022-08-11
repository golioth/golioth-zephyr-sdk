/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <qcbor/qcbor.h>
#include <qcbor/posix_error_map.h>

#include <net/golioth.h>
#include <net/golioth/lightdb_helpers.h>

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
