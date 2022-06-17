/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <net/golioth.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/cbprintf.h>
#include <qcbor/posix_error_map.h>
#include <qcbor/qcbor.h>

/* Set this to 1 if you want to see what is being sent to server */
#define DEBUG_PRINTING 0

#if DEBUG_PRINTING
#define DBG(fmt, ...) printk("%s: "fmt, __func__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif

#define LOGS_URI_PATH		"logs"
#define CBOR_SPACE_RESERVED	8

struct golioth_cbor_ctx {
	QCBOREncodeContext encode_ctx;
	UsefulBuf encode_bufc;
};

struct golioth_pdu_ctx {
	uint8_t *begin;
	uint8_t *ptr;
	uint8_t *end;
};

struct golioth_log_ctx {
	struct golioth_client *client;
	uint32_t msg_index;
	uint32_t msg_part;

	bool panic_mode;

	struct coap_packet coap_packet;
	struct golioth_cbor_ctx cbor;
	struct golioth_pdu_ctx pdu;

	uint8_t packet_buf[CONFIG_LOG_BACKEND_GOLIOTH_MAX_PACKET_SIZE];
};

static const char *level_str(uint32_t level)
{
	switch (level) {
	case LOG_LEVEL_ERR:
		return "error";
	case LOG_LEVEL_WRN:
		return "warn";
	case LOG_LEVEL_INF:
		return "info";
	default:
		return "debug";
	}
}

static int coap_packet_prepare(struct coap_packet *packet, uint8_t *buf,
			       size_t buf_len)
{
	int err;

	err = coap_packet_init(packet, buf, buf_len,
			       COAP_VERSION_1, COAP_TYPE_NON_CON,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       COAP_METHOD_POST, coap_next_id());
	if (err) {
		DBG("failed to init CoAP packet: %d\n", err);
		goto fail;
	}

	err = coap_packet_append_option(packet, COAP_OPTION_URI_PATH,
					LOGS_URI_PATH,
					sizeof(LOGS_URI_PATH) - 1);
	if (err) {
		DBG("failed to append logs uri path: %d\n", err);
		goto fail;
	}

	err = coap_append_option_int(packet, COAP_OPTION_CONTENT_FORMAT,
				     COAP_CONTENT_FORMAT_APP_CBOR);
	if (err) {
		DBG("failed to append logs content format: %d\n", err);
		goto fail;
	}

	err = coap_packet_append_payload_marker(packet);
	if (err) {
		DBG("failed to append logs payload marker: %d\n", err);
		goto fail;
	}

fail:
	return err;
}

static void log_cbor_prepare(struct golioth_cbor_ctx *cbor, uint8_t *buf,
			     size_t buf_len)
{
	cbor->encode_bufc.ptr = buf;
	cbor->encode_bufc.len = buf_len;

	QCBOREncode_Init(&cbor->encode_ctx, cbor->encode_bufc);
}

static void log2_cbor_append_headers(struct golioth_log_ctx *ctx,
				     struct log_msg2 *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	void *source = (void *)log_msg2_get_source(msg);

	if (source) {
		uint8_t domain_id = log_msg2_get_domain(msg);
		int16_t source_id =
			(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ?
				log_dynamic_source_id(source) :
				log_const_source_id(source));

		QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "module",
					     log_source_name_get(domain_id, source_id));
	}

	QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "level",
				     level_str(log_msg2_get_level(msg)));
}

static int log_packet_prepare(struct golioth_log_ctx *ctx)
{
	int err;

	err = coap_packet_prepare(&ctx->coap_packet, ctx->packet_buf,
				  sizeof(ctx->packet_buf));
	if (err) {
		return err;
	}

	/*
	 * Use tail of CoAP packet (where payload will go) to write CBOR
	 * content. This allows to utilize CoAP buffer space directly for
	 * encoding CBOR.
	 */
	log_cbor_prepare(&ctx->cbor, ctx->packet_buf + ctx->coap_packet.offset,
			 sizeof(ctx->packet_buf) - ctx->coap_packet.offset);

	return 0;
}

static int log_packet_finish(struct golioth_log_ctx *ctx)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	size_t encoded_len;
	QCBORError qerr;
	int err;

	qerr = QCBOREncode_FinishGetSize(&cbor->encode_ctx, &encoded_len);
	if (qerr != QCBOR_SUCCESS) {
		DBG("failed to encode: %d (%s)\n", qerr, qcbor_err_to_str(qerr));
		return qcbor_error_to_posix(qerr);
	}

	/*
	 * Add CBOR payload into CoAP payload. In fact CBOR is already in good
	 * place in memory, so the only thing that is needed is moving forward
	 * CoAP offset.
	 *
	 * TODO: add CoAP API that will prevent internal memcpy()
	 */
	err = coap_packet_append_payload(&ctx->coap_packet, cbor->encode_bufc.ptr,
					 encoded_len);
	if (err) {
		DBG("logs payload append fail: %d\n", err);
		return err;
	}

	return 0;
}

static void log_cbor_create_map(struct golioth_log_ctx *ctx)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;

	QCBOREncode_OpenMap(&cbor->encode_ctx);
}

static void log_cbor_close_map(struct golioth_log_ctx *ctx)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;

	QCBOREncode_CloseMap(&cbor->encode_ctx);
}

static int log_pdu_prepare_ext(struct golioth_pdu_ctx *pdu,
			       struct golioth_cbor_ctx *cbor,
			       size_t elements, size_t additional_reserved)
{
	size_t offset;

	offset = UsefulOutBuf_GetEndPosition(&cbor->encode_ctx.OutBuf) +
		CBOR_SPACE_RESERVED * elements +
		additional_reserved;

	pdu->begin = pdu->ptr = (uint8_t *)cbor->encode_bufc.ptr + offset;
	pdu->end = (uint8_t *)cbor->encode_bufc.ptr + cbor->encode_bufc.len;
	if (pdu->begin > pdu->end) {
		DBG("not enough space for encoding PDU\n");
		return -ENOMEM;
	}

	return 0;
}

static inline int log_pdu_prepare(struct golioth_pdu_ctx *pdu,
				  struct golioth_cbor_ctx *cbor)
{
	return log_pdu_prepare_ext(pdu, cbor, 1, 0);
}

static void log_pdu_text_finish(struct golioth_pdu_ctx *pdu,
				struct golioth_cbor_ctx *cbor)
{
	UsefulBufC text = {
		.ptr = pdu->begin,
		.len = pdu->ptr - pdu->begin,
	};

	QCBOREncode_AddText(&cbor->encode_ctx, text);
}

static void log_pdu_bytes_finish(struct golioth_pdu_ctx *pdu,
				 struct golioth_cbor_ctx *cbor)
{
	UsefulBufC bytes = {
		.ptr = pdu->begin,
		.len = pdu->ptr - pdu->begin,
	};

	QCBOREncode_AddBytes(&cbor->encode_ctx, bytes);
}

static int cbprintf_out_func(int c, void *out_ctx)
{
	struct golioth_log_ctx *ctx = out_ctx;
	struct golioth_pdu_ctx *pdu = &ctx->pdu;

	if (pdu->ptr >= pdu->end) {
		/*
		 * TODO: send current packet and create new one
		 */
		DBG("no more space for formatted message\n");

		return 0;
	}

	*pdu->ptr = (uint8_t)c;
	pdu->ptr++;

	__ASSERT_NO_MSG(pdu->ptr <= pdu->end);

	return 0;
}

static const uint8_t *find_colon(const uint8_t *begin, const uint8_t *end)
{
	const uint8_t *p;

	for (p = begin; p < end; p++) {
		if (*p == ':') {
			return p;
		}
	}

	return NULL;
}

static int log_msg2_process(struct golioth_log_ctx *ctx, struct log_msg2 *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	struct golioth_pdu_ctx *pdu = &ctx->pdu;
	uint8_t level = log_msg2_get_level(msg);
	bool raw_string = (level == LOG_LEVEL_INTERNAL_RAW_STRING);
	bool has_func = (BIT(level) & LOG_FUNCTION_PREFIX_MASK);
	int err;

	ctx->msg_part = 0;

	err = log_packet_prepare(ctx);
	if (err) {
		goto finish;
	}

	log_cbor_create_map(ctx);

	QCBOREncode_AddUInt64ToMap(&cbor->encode_ctx, "uptime",
				   log_output_timestamp_to_us(log_msg2_get_timestamp(msg)));

	if (!raw_string) {
		log2_cbor_append_headers(ctx, msg);
	}

	QCBOREncode_AddUInt64ToMap(&cbor->encode_ctx, "index", ctx->msg_index);

	size_t len;
	uint8_t *data = log_msg2_get_package(msg, &len);

	if (len) {
		const uint8_t *func_colon = NULL;

		if (has_func) {
			err = log_pdu_prepare_ext(pdu, cbor, 2, sizeof("func") + 1);
		} else {
			err = log_pdu_prepare(pdu, cbor);
		}

		if (err) {
			goto finish;
		}

		err = cbpprintf(cbprintf_out_func, ctx, data);

		(void)err;
		__ASSERT_NO_MSG(err >= 0);

		if (has_func) {
			func_colon = find_colon(pdu->begin, pdu->ptr);
		}

		if (func_colon) {
			const uint8_t *post_colon = func_colon + sizeof(": ") - 1;
			UsefulBufC func = {
				.ptr = pdu->begin,
				.len = func_colon - pdu->begin,
			};
			UsefulBufC msg = {
				.ptr = post_colon,
				.len = pdu->ptr - post_colon,
			};

			QCBOREncode_AddTextToMap(&cbor->encode_ctx, "func", func);
			QCBOREncode_AddTextToMap(&cbor->encode_ctx, "msg", msg);
		} else {
			QCBOREncode_AddSZString(&cbor->encode_ctx, "msg");
			log_pdu_text_finish(pdu, cbor);
		}
	}

	data = log_msg2_get_data(msg, &len);
	if (len) {
		QCBOREncode_AddSZString(&cbor->encode_ctx, "hexdump");

		err = log_pdu_prepare(pdu, cbor);
		if (err) {
			goto finish;
		}

		if (len > pdu->end - pdu->begin) {
			len = pdu->end - pdu->begin;
		}

		memcpy(pdu->begin, data, len);
		pdu->ptr += len;

		log_pdu_bytes_finish(pdu, cbor);
	}

	log_cbor_close_map(ctx);

	err = log_packet_finish(ctx);
	if (err) {
		goto finish;
	}

	golioth_send_coap(ctx->client, &ctx->coap_packet);

finish:
	ctx->msg_index++;

	return err;
}

static void process(const struct log_backend *const backend,
		    union log_msg2_generic *msg)
{
	struct golioth_log_ctx *ctx = backend->cb->ctx;

	if (ctx->panic_mode) {
		return;
	}

	log_msg2_process(ctx, &msg->log);
}

static const struct log_backend log_backend_golioth;

static void init_golioth(const struct log_backend *const backend)
{
	log_backend_deactivate(&log_backend_golioth);
}

static void panic(struct log_backend const *const backend)
{
	struct golioth_log_ctx *ctx = backend->cb->ctx;

	ctx->panic_mode = true;
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	struct golioth_log_ctx *ctx = backend->cb->ctx;

	ctx->msg_index += cnt;
}

static const struct log_backend_api log_backend_golioth_api = {
	.panic = panic,
	.init = init_golioth,
	.process = process,
	.dropped = dropped,
};

/* Note that the backend can be activated only after we have networking
 * subsystem ready so we must not start it immediately.
 */
LOG_BACKEND_DEFINE(log_backend_golioth, log_backend_golioth_api, false);

static struct golioth_log_ctx log_ctx;

int log_backend_golioth_init(struct golioth_client *client)
{
	log_ctx.client = client;

	log_backend_enable(&log_backend_golioth, &log_ctx, CONFIG_LOG_MAX_LEVEL);

	return 0;
}
