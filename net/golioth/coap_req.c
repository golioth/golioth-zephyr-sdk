/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth);

#include <stdlib.h>

#include <zephyr/random/rand32.h>

#include "coap_req.h"
#include "coap_utils.h"
#include "golioth_utils.h"

#define COAP_RESPONSE_CODE_CLASS(code)	((code) >> 5)

static int golioth_coap_req_send(struct golioth_coap_req *req)
{
	return golioth_send_coap(req->client, &req->request);
}

static void golioth_coap_pending_init(struct golioth_coap_pending *pending,
				      uint8_t retries)
{
	pending->t0 = k_uptime_get_32();
	pending->timeout = 0;
	pending->retries = retries;
}

static void __golioth_coap_req_submit(struct golioth_coap_req *req)
{
	struct golioth_client *client = req->client;

	sys_dlist_append(&client->coap_reqs, &req->node);
}

static void golioth_coap_req_submit(struct golioth_coap_req *req)
{
	struct golioth_client *client = req->client;

	k_mutex_lock(&client->coap_reqs_lock, K_FOREVER);
	__golioth_coap_req_submit(req);
	k_mutex_unlock(&client->coap_reqs_lock);
}

static void golioth_coap_req_cancel(struct golioth_coap_req *req)
{
	sys_dlist_remove(&req->node);
}

static void golioth_coap_req_cancel_and_free(struct golioth_coap_req *req)
{
	LOG_DBG("cancel and free req %p data %p", req, req->request.data);

	golioth_coap_req_cancel(req);
	free(req->request.data);
	free(req);
}

static int golioth_coap_code_to_posix(uint8_t code)
{
	switch (COAP_RESPONSE_CODE_CLASS(code)) {
	case 2:
		return 0;
	case 4:
		switch (code) {
		case COAP_RESPONSE_CODE_BAD_REQUEST:
			return -EFAULT;
		case COAP_RESPONSE_CODE_UNAUTHORIZED:
			return -EACCES;
		case COAP_RESPONSE_CODE_BAD_OPTION:
			return -EINVAL;
		case COAP_RESPONSE_CODE_FORBIDDEN:
			return -EACCES;
		case COAP_RESPONSE_CODE_NOT_FOUND:
			return -ENOENT;
		case COAP_RESPONSE_CODE_NOT_ALLOWED:
			return -EACCES;
		case COAP_RESPONSE_CODE_NOT_ACCEPTABLE:
			return -EACCES;
		case COAP_RESPONSE_CODE_INCOMPLETE:
			return -EINVAL;
		case COAP_RESPONSE_CODE_CONFLICT:
			return -EBUSY;
		case COAP_RESPONSE_CODE_PRECONDITION_FAILED:
			return -EACCES;
		case COAP_RESPONSE_CODE_REQUEST_TOO_LARGE:
			return -E2BIG;
		case COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT:
			return -ENOTSUP;
		case COAP_RESPONSE_CODE_UNPROCESSABLE_ENTITY:
			return -EBADMSG;
		case COAP_RESPONSE_CODE_TOO_MANY_REQUESTS:
			return -EBUSY;
		}
		__fallthrough;
	case 5:
		return -EBADMSG;
	default:
		LOG_ERR("Unknown CoAP response code class (%u)",
			(unsigned int)COAP_RESPONSE_CODE_CLASS(code));
		return -EBADMSG;
	}
}

static int golioth_coap_req_append_block2_option(struct golioth_coap_req *req)
{
	if (req->request_wo_block2.offset) {
		/*
		 * Block2 was already appended once, so just copy state before
		 * it was done.
		 */
		req->request = req->request_wo_block2;
	} else {
		/*
		 * Block2 is about to be appeneded for the first time, so
		 * remember coap_packet state before adding this option.
		 */
		req->request_wo_block2 = req->request;
	}

	return coap_append_block2_option(&req->request, &req->block_ctx);
}

static int golioth_coap_req_next_block(void *data, int status)
{
	struct golioth_coap_req *req = data;
	uint16_t next_id = coap_next_id();
	int err;

	if (status) {
		LOG_WRN("Handle non-zero (%d) status", status);
	}

	if (req->is_observe) {
		struct golioth_req_rsp rsp = {
			.user_data = req->user_data,
			.err = -ENOTSUP,
		};

		(void)req->cb(&rsp);

		return 0;
	}

	coap_packet_set_id(&req->request, next_id);

	err = golioth_coap_req_append_block2_option(req);
	if (err) {
		return err;
	}

	golioth_coap_pending_init(&req->pending, 3);

	return 0;
}

/* Reordering according to RFC7641 section 3.4 but without timestamp comparison */
static inline bool is_newer(int v1, int v2)
{
	return (v1 < v2 && v2 - v1 < (1 << 23)) ||
		(v1 > v2 && v1 - v2 > (1 << 23));
}

static int golioth_coap_req_reply_handler(struct golioth_coap_req *req,
					  const struct coap_packet *response)
{
	uint16_t payload_len;
	uint8_t code;
	const uint8_t *payload;
	int block2;
	int err;

	code = coap_header_get_code(response);

	LOG_DBG("CoAP response code: 0x%x (class %u detail %u)",
		(unsigned int)code, (unsigned int)(code >> 5), (unsigned int)(code & 0x1f));

	err = golioth_coap_code_to_posix(code);
	if (err) {
		struct golioth_req_rsp rsp = {
			.user_data = req->user_data,
			.err = err,
		};

		(void)req->cb(&rsp);

		LOG_INF("cancel and free req: %p", req);

		goto cancel_and_free;
	}

	payload = coap_packet_get_payload(response, &payload_len);

	block2 = coap_get_option_int(response, COAP_OPTION_BLOCK2);
	if (block2 != -ENOENT) {
		size_t new_offset;
		size_t cur_offset;

		err = coap_update_from_block(response, &req->block_ctx);
		if (err) {
			struct golioth_req_rsp rsp = {
				.user_data = req->user_data,
				.err = -EBADMSG,
			};

			LOG_ERR("Failed to parse get response: %d", err);

			(void)req->cb(&rsp);

			err = -EBADMSG;
			goto cancel_and_free;
		}

		cur_offset = req->block_ctx.current;

		new_offset = coap_next_block_for_option(response, &req->block_ctx,
							COAP_OPTION_BLOCK2);
		if (new_offset < 0) {
			struct golioth_req_rsp rsp = {
				.user_data = req->user_data,
				.err = -EBADMSG,
			};

			LOG_ERR("Failed to move to next block: %d", new_offset);

			(void)req->cb(&rsp);

			err = -EBADMSG;
			goto cancel_and_free;
		} else if (new_offset == 0) {
			struct golioth_req_rsp rsp = {
				.data = payload,
				.len = payload_len,
				.off = cur_offset,

				.total = req->block_ctx.total_size,

				.get_next = NULL,
				.get_next_data = NULL,

				.user_data = req->user_data,
			};

			LOG_DBG("Blockwise transfer is finished!");

			(void)req->cb(&rsp);

			goto cancel_and_free;
		} else {
			struct golioth_req_rsp rsp = {
				.data = payload,
				.len = payload_len,
				.off = cur_offset,

				.total = req->block_ctx.total_size,

				.get_next = golioth_coap_req_next_block,
				.get_next_data = req,

				.user_data = req->user_data,

				.err = req->is_observe ? -EMSGSIZE : 0,
			};

			err = req->cb(&rsp);
			if (err) {
				LOG_WRN("Received error (%d) from callback, cancelling", err);
				goto cancel_and_free;
			}

			if (req->is_observe) {
				LOG_ERR("TODO: blockwise observe is not supported");
				err = -ENOTSUP;
				goto cancel_and_free;
			}

			return 0;
		}
	} else {
		struct golioth_req_rsp rsp = {
			.data = payload,
			.len = payload_len,
			.off = 0,

			/* Is it the same as 'req->block_ctx.total_size' ? */
			.total = payload_len,

			.get_next = NULL,
			.get_next_data = NULL,

			.user_data = req->user_data,
		};

		(void)req->cb(&rsp);

		goto cancel_and_free;
	}

cancel_and_free:
	if (req->is_observe && !err) {
		req->is_pending = false;
	} else {
		golioth_coap_req_cancel_and_free(req);
	}

	return 0;
}

void golioth_coap_req_process_rx(struct golioth_client *client, const struct coap_packet *rx)
{
	struct golioth_coap_req *req;
	uint8_t rx_token[COAP_TOKEN_MAX_LEN];
	uint16_t rx_id;
	uint8_t rx_tkl;

	rx_id = coap_header_get_id(rx);
	rx_tkl = coap_header_get_token(rx, rx_token);

	k_mutex_lock(&client->coap_reqs_lock, K_FOREVER);

	SYS_DLIST_FOR_EACH_CONTAINER(&client->coap_reqs, req, node) {
		uint16_t req_id = coap_header_get_id(&req->request);
		uint8_t req_token[COAP_TOKEN_MAX_LEN];
		uint8_t req_tkl = coap_header_get_token(&req->request, req_token);
		int age;

		if (req_id == 0U && req_tkl == 0U) {
			continue;
		}

		/* Piggybacked must match id when token is empty */
		if (req_id != rx_id && rx_tkl == 0U) {
			continue;
		}

		if (rx_tkl > 0 && memcmp(req_token, rx_token, rx_tkl)) {
			continue;
		}

		age = coap_get_option_int(rx, COAP_OPTION_OBSERVE);
		/* handle observed requests only if received in order */
		if (age == -ENOENT || is_newer(req->reply.age, age)) {
			req->reply.age = age;
			golioth_coap_req_reply_handler(req, rx);
		}

		break;
	}

	k_mutex_unlock(&client->coap_reqs_lock);
}

static int golioth_coap_req_init(struct golioth_coap_req *req,
				 struct golioth_client *client,
				 enum coap_method method,
				 enum coap_msgtype msg_type,
				 uint8_t *buffer, size_t buffer_len,
				 golioth_req_cb_t cb, void *user_data)
{
	int err;

	err = coap_packet_init(&req->request, buffer, buffer_len,
			       COAP_VERSION_1, msg_type,
			       COAP_TOKEN_MAX_LEN, coap_next_token(),
			       method, coap_next_id());
	if (err) {
		return err;
	}

	req->client = client;
	req->cb = cb;
	req->user_data = user_data;
	req->request_wo_block2.offset = 0;
	req->reply.age = 0;

	coap_block_transfer_init(&req->block_ctx, golioth_estimated_coap_block_size(client), 0);

	return 0;
}

int golioth_coap_req_schedule(struct golioth_coap_req *req)
{
	struct golioth_client *client = req->client;

	golioth_coap_pending_init(&req->pending, 3);

	golioth_coap_req_submit(req);

	if (client->wakeup) {
		client->wakeup(client);
	}

	return 0;
}

int golioth_coap_req_new(struct golioth_coap_req **req,
			 struct golioth_client *client,
			 enum coap_method method,
			 enum coap_msgtype msg_type,
			 size_t buffer_len,
			 golioth_req_cb_t cb, void *user_data)
{
	uint8_t *buffer;
	int err;

	*req = calloc(1, sizeof(**req));
	if (!(*req)) {
		LOG_ERR("Failed to allocate request");
		return -ENOMEM;
	}

	buffer = malloc(buffer_len);
	if (!buffer) {
		LOG_ERR("Failed to allocate packet buffer");
		err = -ENOMEM;
		goto free_req;
	}

	err = golioth_coap_req_init(*req, client, method, msg_type,
				    buffer, buffer_len,
				    cb, user_data);
	if (err) {
		LOG_ERR("Failed to initialize CoAP GET request: %d", err);
		goto free_buffer;
	}

	return 0;

free_buffer:
	free(buffer);

free_req:
	free(*req);

	return err;
}

void golioth_coap_req_free(struct golioth_coap_req *req)
{
	free(req->request.data); /* buffer */
	free(req);
}

int golioth_coap_req_cb(struct golioth_client *client,
			enum coap_method method,
			const uint8_t **pathv,
			enum golioth_content_format format,
			const uint8_t *data, size_t data_len,
			golioth_req_cb_t cb, void *user_data,
			int flags)
{
	size_t path_len = coap_pathv_estimate_alloc_len(pathv);
	struct golioth_coap_req *req;
	int err;

	err = golioth_coap_req_new(&req, client, method, COAP_TYPE_CON,
				   GOLIOTH_COAP_MAX_NON_PAYLOAD_LEN + path_len + data_len,
				   cb, user_data);
	if (err) {
		LOG_ERR("Failed to create new CoAP GET request: %d", err);
		goto free_req;
	}

	if (method == COAP_METHOD_GET && (flags & GOLIOTH_COAP_REQ_OBSERVE)) {
		req->is_observe = true;
		req->is_pending = true;

		err = coap_append_option_int(&req->request, COAP_OPTION_OBSERVE, 0 /* register */);
		if (err) {
			LOG_ERR("Unable add observe option");
			goto free_req;
		}
	}

	err = coap_packet_append_uri_path_from_pathv(&req->request, pathv);
	if (err) {
		LOG_ERR("Unable add uri path to packet");
		goto free_req;
	}

	if (method != COAP_METHOD_GET && method != COAP_METHOD_DELETE) {
		err = coap_append_option_int(&req->request, COAP_OPTION_CONTENT_FORMAT, format);
		if (err) {
			LOG_ERR("Unable add content format to packet");
			goto free_req;
		}
	}

	if (!(flags & GOLIOTH_COAP_REQ_NO_RESP_BODY)) {
		err = coap_append_option_int(&req->request, COAP_OPTION_ACCEPT, format);
		if (err) {
			LOG_ERR("Unable add content format to packet");
			goto free_req;
		}
	}

	if (data && data_len) {
		err = coap_packet_append_payload_marker(&req->request);
		if (err) {
			LOG_ERR("Unable add payload marker to packet");
			goto free_req;
		}

		err = coap_packet_append_payload(&req->request, data, data_len);
		if (err) {
			LOG_ERR("Unable add payload to packet");
			goto free_req;
		}
	}

	return golioth_coap_req_schedule(req);

free_req:
	golioth_coap_req_free(req);

	return err;
}

struct golioth_req_sync_data {
	struct k_sem sem;
	int err;

	golioth_req_cb_t cb;
	void *user_data;
};

static int golioth_req_sync_cb(struct golioth_req_rsp *rsp)
{
	struct golioth_req_sync_data *sync_data = rsp->user_data;
	int err = 0;

	if (rsp->err) {
		sync_data->err = rsp->err;
		goto finish_sync_call;
	}

	if (sync_data->cb) {
		rsp->user_data = sync_data->user_data;

		err = sync_data->cb(rsp);
		if (err) {
			goto finish_sync_call;
		}
	}

	if (rsp->get_next) {
		rsp->get_next(rsp->get_next_data, 0);
		return 0;
	}

finish_sync_call:
	k_sem_give(&sync_data->sem);

	return err;
}

int golioth_coap_req_sync(struct golioth_client *client,
			  enum coap_method method,
			  const uint8_t **pathv,
			  enum golioth_content_format format,
			  const uint8_t *data, size_t data_len,
			  golioth_req_cb_t cb, void *user_data,
			  int flags)
{
	struct golioth_req_sync_data sync_data = {
		.cb = cb,
		.user_data = user_data,
	};
	int err;

	k_sem_init(&sync_data.sem, 0, 1);

	err = golioth_coap_req_cb(client, method, pathv, format,
				  data, data_len,
				  golioth_req_sync_cb, &sync_data,
				  flags);
	if (err) {
		LOG_WRN("Failed to make CoAP request: %d", err);
		return err;
	}

	k_sem_take(&sync_data.sem, K_FOREVER);

	if (sync_data.err) {
		LOG_WRN("req_sync finished with error %d", sync_data.err);

		return sync_data.err;
	}

	return 0;
}

static uint32_t init_ack_timeout(void)
{
#if defined(CONFIG_COAP_RANDOMIZE_ACK_TIMEOUT)
	const uint32_t max_ack = CONFIG_COAP_INIT_ACK_TIMEOUT_MS *
				 CONFIG_COAP_ACK_RANDOM_PERCENT / 100;
	const uint32_t min_ack = CONFIG_COAP_INIT_ACK_TIMEOUT_MS;

	/* Randomly generated initial ACK timeout
	 * ACK_TIMEOUT < INIT_ACK_TIMEOUT < ACK_TIMEOUT * ACK_RANDOM_FACTOR
	 * Ref: https://tools.ietf.org/html/rfc7252#section-4.8
	 */
	return min_ack + (sys_rand32_get() % (max_ack - min_ack));
#else
	return CONFIG_COAP_INIT_ACK_TIMEOUT_MS;
#endif /* defined(CONFIG_COAP_RANDOMIZE_ACK_TIMEOUT) */
}

static bool golioth_coap_pending_cycle(struct golioth_coap_pending *pending)
{
	if (pending->timeout == 0) {
		/* Initial transmission. */
		pending->timeout = init_ack_timeout();

		return true;
	}

	if (pending->retries == 0) {
		return false;
	}

	pending->t0 += pending->timeout;
	pending->timeout = pending->timeout << 1;
	pending->retries--;

	return true;
}

static int64_t golioth_coap_req_poll_prepare(struct golioth_coap_req *req, uint32_t now)
{
	int64_t timeout;
	bool send = false;
	bool resend = (req->pending.timeout != 0);
	int err;

	while (true) {
		timeout = (int32_t)(req->pending.t0 + req->pending.timeout) - (int32_t)now;

		if (timeout > 0) {
			/* Return timeout when packet still waits for response/ack */
			break;
		}

		send = golioth_coap_pending_cycle(&req->pending);
		if (!send) {
			struct golioth_req_rsp rsp = {
				.user_data = req->user_data,
				.err = -ETIMEDOUT,
			};

			LOG_WRN("Packet %p (reply %p) was not replied to", req, &req->reply);

			(void)req->cb(&rsp);

			golioth_coap_req_cancel_and_free(req);

			return INT64_MAX;
		}
	}

	if (send) {
		if (resend) {
			LOG_WRN("Resending request %p (reply %p) (retries %d)",
				req, &req->reply,
				(int)req->pending.retries);
		}

		err = golioth_coap_req_send(req);
		if (err) {
			LOG_ERR("Send error: %d", err);
		}
	}

	return timeout;
}

static int64_t __golioth_coap_reqs_poll_prepare(struct golioth_client *client, int64_t now)
{
	struct golioth_coap_req *req, *next;
	int64_t min_timeout = INT64_MAX;

	SYS_DLIST_FOR_EACH_CONTAINER_SAFE(&client->coap_reqs, req, next, node) {
		if (req->is_observe && !req->is_pending) {
			continue;
		}

		int64_t req_timeout = golioth_coap_req_poll_prepare(req, now);

		min_timeout = MIN(min_timeout, req_timeout);
	}

	return min_timeout;
}

int64_t golioth_coap_reqs_poll_prepare(struct golioth_client *client, int64_t now)
{
	int64_t timeout;

	k_mutex_lock(&client->coap_reqs_lock, K_FOREVER);
	timeout = __golioth_coap_reqs_poll_prepare(client, now);
	k_mutex_unlock(&client->coap_reqs_lock);

	return timeout;
}
