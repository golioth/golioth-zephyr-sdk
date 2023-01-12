/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(libcoap_hello, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>

#include <samples/common/net_connect.h>

#include <coap3/coap.h>

#define LOG_SOCKADDR(fmt, addr)						\
	do {								\
		char buf[NET_IPV6_ADDR_LEN];				\
									\
		if (addr->sa_family == AF_INET6) {			\
			net_addr_ntop(AF_INET6, &net_sin6(addr)->sin6_addr, \
				      buf, sizeof(buf));		\
		} else if (addr->sa_family == AF_INET) {		\
			net_addr_ntop(AF_INET, &net_sin(addr)->sin_addr, \
				      buf, sizeof(buf));		\
		}							\
									\
		LOG_DBG(fmt, buf);					\
	} while (0)

static coap_session_t *get_session(coap_context_t *ctx, const char *host) {
	int s;
	struct zsock_addrinfo hints;
	struct zsock_addrinfo *result, *rp;
	coap_session_t *session;

	if (!ctx) {
		return NULL;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_DGRAM; /* Coap uses UDP */
	hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ALL;

	s = zsock_getaddrinfo(host, NULL, &hints, &result);
	if ( s != 0 ) {
		LOG_ERR("zsock_getaddrinfo: %s", zsock_gai_strerror(s));
		return NULL;
	}

	/* iterate through results until success */
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		coap_address_t addr;
		coap_address_init(&addr);
		addr.size = rp->ai_addrlen;
		memcpy(&addr.addr, rp->ai_addr, rp->ai_addrlen);

		addr.addr.sin.sin_port = htons (COAP_DEFAULT_PORT);

		LOG_INF("Family of addr %d", (int) rp->ai_addr->sa_family);
		LOG_SOCKADDR("Trying addr '%s'", rp->ai_addr);

		session = coap_new_client_session(ctx, NULL, &addr, COAP_PROTO_UDP);
		if (!session) {
			LOG_ERR("No sesssion?");
			continue;
		}

		zsock_freeaddrinfo(result);

		return session;
	}

	LOG_ERR("no session available for host '%s'", host);
	zsock_freeaddrinfo(result);
	return NULL;
}

static void coap_add_path(coap_pdu_t *pdu, const char* path)
{
	size_t path_len = strlen(path);
	unsigned char buf[64];
	unsigned char* pbuf = buf;
	size_t buflen = sizeof(buf);
	int nsegments;

	nsegments = coap_split_path((const uint8_t*)path, path_len, pbuf, &buflen);

	while (nsegments--) {
		if (coap_opt_length(pbuf) > 0) {
			coap_add_option(pdu, COAP_OPTION_URI_PATH,
					coap_opt_length(pbuf), coap_opt_value(pbuf));
		}
		pbuf += coap_opt_size(pbuf);
	}
}

static void send_one(coap_session_t *session)
{
	coap_pdu_t *pdu;
	uint8_t token[8];
	size_t token_len;

	pdu = coap_pdu_init(COAP_MESSAGE_NON, COAP_REQUEST_CODE_POST,
			    coap_new_message_id(session), COAP_DEFAULT_MTU);
	if (!pdu) {
		LOG_ERR("Cannot init PDU");
		return;
	}

	/* Token */
	coap_session_new_token(session, &token_len, token);
	coap_add_token(pdu, token_len, token);

	/* Path */
	coap_add_path(pdu, "hello");

	/* Payload */
	coap_add_data(pdu, sizeof("Hello1") - 1, "Hello!");

	coap_send(session, pdu);
}

void main(void)
{
	int counter = 0;
	coap_context_t *ctx;
	coap_session_t *session;

	coap_set_log_level(COAP_LOG_DTLS_BASE);

	coap_log_info("coap info\n");

	LOG_DBG("Start libcoap hello sample");

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	ctx = coap_new_context(NULL);
	if (!ctx) {
		LOG_ERR("Cannot get new context");
		return;
	}

	LOG_INF("Got new context %p", (void *)ctx);

	session = get_session(ctx, "10.11.12.10");
	if (!session) {
		LOG_ERR("Cannot get new session");
		return;
	}

	while (true) {
		LOG_INF("Sending hello! %d", counter);

		LOG_INF("Sending hello");
		send_one(session);
		LOG_INF("Sent");

		++counter;

		for (int i = 0; i < 5; i++) {
			k_sleep(K_SECONDS(1));

			coap_io_process(ctx, COAP_IO_NO_WAIT);
		}
	}
}
