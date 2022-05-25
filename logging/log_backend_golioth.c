/*
 * Copyright (c) 2021-2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <irq.h>
#include <logging/log_backend.h>
#include <logging/log_ctrl.h>
#include <logging/log_core.h>
#include <logging/log_output.h>
#include <net/golioth.h>
#include <sys/cbprintf.h>
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

static void log_cbor_append_index(struct golioth_log_ctx *ctx)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;

	QCBOREncode_AddUInt64ToMap(&cbor->encode_ctx, "index", ctx->msg_index);
}

static void log_cbor_append_headers(struct golioth_log_ctx *ctx,
				    struct log_msg *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;

	QCBOREncode_AddUInt64ToMap(&cbor->encode_ctx, "uptime",
				   log_output_timestamp_to_us(log_msg_timestamp_get(msg)));

	QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "module",
				     log_source_name_get(log_msg_domain_id_get(msg),
							 log_msg_source_id_get(msg)));

	QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "level",
				     level_str(msg->hdr.ids.level));
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

static void log_cbor_append_func(struct golioth_log_ctx *ctx, const char *func)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;

	QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "func", func);
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

static int append_formatted_str(struct golioth_log_ctx *ctx,
				const char *fmt, ...)
{
	va_list args;
	int length = 0;

	va_start(args, fmt);
	length = cbvprintf(cbprintf_out_func, ctx, fmt, args);
	va_end(args);

	return length;
}

static void log_format(struct golioth_log_ctx *ctx, const char *str,
		       uint32_t nargs, uint32_t *args)
{
	switch (nargs) {
	case 0:
		append_formatted_str(ctx, str);
		break;
	case 1:
		append_formatted_str(ctx, str, args[0]);
		break;
	case 2:
		append_formatted_str(ctx, str, args[0],
				args[1]);
		break;
	case 3:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2]);
		break;
	case 4:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3]);
		break;
	case 5:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4]);
		break;
	case 6:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5]);
		break;
	case 7:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6]);
		break;
	case 8:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7]);
		break;
	case 9:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8]);
		break;
	case 10:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9]);
		break;
	case 11:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9], args[10]);
		break;
	case 12:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9], args[10],
				args[11]);
		break;
	case 13:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9], args[10],
				args[11], args[12]);
		break;
	case 14:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9], args[10],
				args[11], args[12], args[13]);
		break;
	case 15:
		append_formatted_str(ctx, str, args[0],
				args[1], args[2], args[3], args[4], args[5],
				args[6], args[7], args[8], args[9], args[10],
				args[11], args[12], args[13], args[14]);
		break;
	default:
		/* Unsupported number of arguments. */
		__ASSERT_NO_MSG(true);
		break;
	}
}

static int std_encode(struct golioth_log_ctx *ctx, struct log_msg *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	struct golioth_pdu_ctx *pdu = &ctx->pdu;
	const char *str = log_msg_str_get(msg);
	uint32_t nargs = log_msg_nargs_get(msg);
	uint32_t *args = alloca(sizeof(uint32_t)*nargs);
	uint32_t level = log_msg_level_get(msg);
	bool has_func = (BIT(level) & LOG_FUNCTION_PREFIX_MASK);
	int err;

	for (int i = 0; i < nargs; i++) {
		args[i] = log_msg_arg_get(msg, i);
	}

	log_cbor_create_map(ctx);

	log_cbor_append_index(ctx);
	log_cbor_append_headers(ctx, msg);

	if (has_func) {
		log_cbor_append_func(ctx, (void *)args[0]);

		/* move after '%s: ' prefix */
		str += sizeof("%s: ") - 1;

		/* consume function name pointer */
		args++;
		nargs--;
	}

	QCBOREncode_AddSZString(&cbor->encode_ctx, "msg");

	err = log_pdu_prepare(pdu, cbor);
	if (err) {
		return err;
	}

	log_format(ctx, str, nargs, args);

	log_pdu_text_finish(pdu, cbor);

	log_cbor_close_map(ctx);

	return 0;
}

static int raw_string_encode(struct golioth_log_ctx *ctx, struct log_msg *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	struct golioth_pdu_ctx *pdu = &ctx->pdu;
	size_t length;
	int err;

	log_cbor_create_map(ctx);

	log_cbor_append_index(ctx);

	QCBOREncode_AddSZString(&cbor->encode_ctx, "msg");

	err = log_pdu_prepare(pdu, cbor);
	if (err) {
		return err;
	}

	length = pdu->end - pdu->begin;
	log_msg_hexdump_data_get(msg, pdu->begin, &length, 0);
	if (length > 0) {
		/* Trim newline */
		if (pdu->ptr[length - 1] == '\n') {
			length--;
		}

		pdu->ptr += length;
	}

	log_pdu_text_finish(pdu, cbor);

	log_cbor_close_map(ctx);

	return 0;
}

static int hexdump_encode(struct golioth_log_ctx *ctx, struct log_msg *msg)
{
	struct golioth_cbor_ctx *cbor = &ctx->cbor;
	struct golioth_pdu_ctx *pdu = &ctx->pdu;
	size_t length;
	int err;

	log_cbor_create_map(ctx);

	log_cbor_append_index(ctx);
	log_cbor_append_headers(ctx, msg);

	QCBOREncode_AddSZStringToMap(&cbor->encode_ctx, "msg", log_msg_str_get(msg));

	QCBOREncode_AddSZString(&cbor->encode_ctx, "hexdump");

	err = log_pdu_prepare(pdu, cbor);
	if (err) {
		return err;
	}

	length = pdu->end - pdu->begin;
	log_msg_hexdump_data_get(msg, pdu->begin, &length, 0);
	pdu->ptr += length;

	log_pdu_bytes_finish(pdu, cbor);

	log_cbor_close_map(ctx);

	return 0;
}

static int log_msg_process(struct golioth_log_ctx *ctx, struct log_msg *msg)
{
	uint8_t level = (uint8_t)log_msg_level_get(msg);
	bool raw_string = (level == LOG_LEVEL_INTERNAL_RAW_STRING);
	int err;

	log_msg_get(msg);

	ctx->msg_part = 0;

	err = log_packet_prepare(ctx);
	if (err) {
		goto finish;
	}

	if (log_msg_is_std(msg)) {
		err = std_encode(ctx, msg);
	} else if (raw_string) {
		err = raw_string_encode(ctx, msg);
	} else {
		err = hexdump_encode(ctx, msg);
	}

	if (err) {
		goto finish;
	}

	err = log_packet_finish(ctx);
	if (err) {
		goto finish;
	}

	golioth_send_coap(ctx->client, &ctx->coap_packet);

finish:
	ctx->msg_index++;

	log_msg_put(msg);

	return err;
}

static void send_output(const struct log_backend *const backend,
			struct log_msg *msg)
{
	struct golioth_log_ctx *ctx = backend->cb->ctx;

	if (ctx->panic_mode) {
		return;
	}

	log_msg_process(ctx, msg);
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
	.process = IS_ENABLED(CONFIG_LOG2) ? process : NULL,
	.put = IS_ENABLED(CONFIG_LOG_MODE_DEFERRED) ? send_output : NULL,
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

const struct log_backend *log_backend_golioth_get(void)
{
	return &log_backend_golioth;
}
