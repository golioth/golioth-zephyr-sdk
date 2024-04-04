/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth_shell);

#include <logging/golioth.h>
#include <net/golioth.h>
#include <stdlib.h>
#include <zephyr/net/coap.h>
#include <zephyr/shell/shell.h>

#include "common.h"

static struct golioth_client golioth_shell_client;
struct golioth_client *client = &golioth_shell_client;

static int cmd_golioth_connect(const struct shell *sh, size_t argc, char **argv)
{
	const char *host = argv[1];
	unsigned long port = 5684;
	int err;

	if (argc > 2) {
		const char *port_s = argv[2];

		port = strtoul(port_s, NULL, 0);
	}

	err = golioth_connect(client, host, port);
	if (err) {
		if (err == -EALREADY) {
			shell_warn(sh, "Already connected");
		} else {
			shell_error(sh, "Failed to connect: %d", err);
		}

		return err;
	}

	return 0;
}

static int cmd_golioth_disconnect(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = golioth_disconnect(client);
	if (err) {
		if (err == -ENOTCONN) {
			shell_warn(sh, "Already disconnected");
		} else {
			shell_error(sh, "Failed to disconnect: %d", err);
		}

		return err;
	}

	return 0;
}

static int cmd_golioth_is_connected(const struct shell *sh, size_t argc, char **argv)
{
	bool connected;

	connected = golioth_is_connected(client);
	shell_print(sh, "%s", connected ? "Connected" : "Disconnected");

	return 0;
}

static int cmd_golioth_set_coap_dtls(const struct shell *sh, size_t argc, char **argv)
{
	static sec_tag_t *tags;
	static size_t num_tags;
	int err;

	/*
	 * 'tags' is either NULL or a valid pointer to heap.
	 * In the former case free(NULL) will be a noop, so we call it unconditionally.
	 */
	free(tags);
	num_tags = 0;

	num_tags = (argc - 1);
	tags = malloc(num_tags * sizeof(*tags));

	for (int i = 1; i < argc; i++) {
		long tag = strtol(argv[i], NULL, 0);

		tags[i - 1] = tag;
	}

	err = golioth_set_proto_coap_dtls(client, tags, num_tags);
	if (err) {
		shell_error(sh, "Failed set CoAP DTLS: %d", err);
		return err;
	}

	return 0;
}

static int cmd_golioth_process_rx(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = golioth_process_rx(client);
	if (err) {
		shell_error(sh, "Failed to process RX: %d", err);
		return err;
	}

	return 0;
}

static int cmd_golioth_ping(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = golioth_ping(client);
	if (err) {
		shell_error(sh, "Failed to ping: %d", err);
		return err;
	}

	return 0;
}

static int cmd_golioth_hello(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	err = golioth_send_hello(client);
	if (err) {
		shell_error(sh, "Failed to send hello: %d", err);
		return err;
	}

	return 0;
}

static int cmd_golioth_log_backend_init(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	if (!IS_ENABLED(CONFIG_LOG_BACKEND_GOLIOTH)) {
		shell_error(sh, "Golioth log backend is not enabled");
		return -ENOSYS;
	}

	err = log_backend_golioth_init(client);
	if (err) {
		shell_error(sh, "Failed to initialize Golioth log backend: %d", err);
		return err;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(cmds_golioth,
	SHELL_CMD_ARG(connect, NULL,
		"Connect", cmd_golioth_connect, 2, 1),
	SHELL_CMD_ARG(disconnect, NULL,
		"Disconnect", cmd_golioth_disconnect, 1, 0),
	SHELL_CMD_ARG(is_connected, NULL,
		"Is connected?", cmd_golioth_is_connected, 1, 0),
	SHELL_CMD(set_coap_dtls, NULL,
		"Set CoAP DTLS", cmd_golioth_set_coap_dtls),
	SHELL_CMD_ARG(process_rx, NULL,
		"Process RX", cmd_golioth_process_rx, 1, 0),
	SHELL_CMD_ARG(ping, NULL,
		"Ping", cmd_golioth_ping, 1, 0),
	SHELL_CMD_ARG(hello, NULL,
		"Send hello", cmd_golioth_hello, 1, 0),
	SHELL_CMD_ARG(log_backend_init, NULL,
		"Initialize log backend", cmd_golioth_log_backend_init, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(golioth, &cmds_golioth, "Golioth shell", NULL);
