#ifndef __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_PRIV_H__
#define __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_PRIV_H__

#include <stdint.h>

struct golioth_client;

struct _QCBORDecodeContext;
typedef struct _QCBORDecodeContext QCBORDecodeContext;

struct _QCBOREncodeContext;
typedef struct _QCBOREncodeContext QCBOREncodeContext;

int golioth_lightdb_get_basic(struct golioth_client *client, const uint8_t *path,
			      void *value_void,
			      void (*get)(QCBORDecodeContext *dec, void *value_void));

int golioth_lightdb_get_int64_t_clamp(struct golioth_client *client, const uint8_t *path,
				      int64_t *value, int64_t min, int64_t max);
int golioth_lightdb_get_uint64_t_clamp(struct golioth_client *client, const uint8_t *path,
				       uint64_t *value, uint64_t max);

void golioth_lightdb_qcbor_get_bool(QCBORDecodeContext *dec, void *value_void);
void golioth_lightdb_qcbor_get_int64_t(QCBORDecodeContext *dec, void *value_void);
void golioth_lightdb_qcbor_get_uint64_t(QCBORDecodeContext *dec, void *value_void);
void golioth_lightdb_qcbor_get_double(QCBORDecodeContext *dec, void *value_void);

int golioth_lightdb_set_basic(struct golioth_client *client, const uint8_t *path,
			      const void *value_void,
			      void (*add)(QCBOREncodeContext *enc,
					  const void *value_void));

void golioth_lightdb_qcbor_add_bool(QCBOREncodeContext *enc, const void *value_void);
void golioth_lightdb_qcbor_add_int64_t(QCBOREncodeContext *enc, const void *value_void);
void golioth_lightdb_qcbor_add_uint64_t(QCBOREncodeContext *enc, const void *value_void);
void golioth_lightdb_qcbor_add_float(QCBOREncodeContext *enc, const void *value_void);
void golioth_lightdb_qcbor_add_double(QCBOREncodeContext *enc, const void *value_void);
void golioth_lightdb_qcbor_add_stringz(QCBOREncodeContext *enc, const void *value_void);

#endif /* __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_PRIV_H__ */
