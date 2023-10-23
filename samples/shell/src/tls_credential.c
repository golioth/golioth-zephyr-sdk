/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(golioth_shell);

#include <stdlib.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/base64.h>

#ifndef SHELL_VT100_ASCII_CTRL_D
#define SHELL_VT100_ASCII_CTRL_D	(0x04)
#endif

enum bypass_content_format {
	BYPASS_CONTENT_FORMAT_TEXT,
	BYPASS_CONTENT_FORMAT_BASE64,
};

struct bypass_content_format_info {
	const char *name;
	enum bypass_content_format content_format;
};

static const struct bypass_content_format_info bypass_content_formats[] = {
	{ "text", BYPASS_CONTENT_FORMAT_TEXT },
	{ "base64", BYPASS_CONTENT_FORMAT_BASE64 },
};

struct bypass_ctx {
	long tag;
	enum tls_credential_type type;
	uint8_t *data;
	size_t len;
	enum bypass_content_format content_format;
	uint8_t base64_buf[4];
	uint8_t base64_buf_len;
};

struct tls_credential_type_info {
	const char *name;
	enum tls_credential_type type;
};

static struct bypass_ctx shell_bypass_context;

static const struct tls_credential_type_info credential_type_info[] = {
	{"none", TLS_CREDENTIAL_NONE},
	{"ca-certificate", TLS_CREDENTIAL_CA_CERTIFICATE},
	{"server-certificate", TLS_CREDENTIAL_SERVER_CERTIFICATE},
	{"private-key", TLS_CREDENTIAL_PRIVATE_KEY},
	{"psk", TLS_CREDENTIAL_PSK},
	{"psk-id", TLS_CREDENTIAL_PSK_ID},
};

static enum tls_credential_type tls_credential_str_to_type(const char *str)
{
	const struct tls_credential_type_info *info;

	for (info = credential_type_info;
	     info < &credential_type_info[ARRAY_SIZE(credential_type_info)];
	     info++) {
		if (!strcmp(info->name, str)) {
			return info->type;
		}
	}

	return TLS_CREDENTIAL_NONE;
}

static int bypass_consume_text(const struct shell *sh, struct bypass_ctx *ctx,
			       uint8_t *data, size_t len)
{
	void *old_data = ctx->data;

	ctx->data = realloc(old_data, ctx->len + len);
	if (!ctx->data) {
		free(old_data);
		ctx->len = 0;
		shell_error(sh, "Not enough memory");
		return -ENOMEM;
	}

	memcpy(&ctx->data[ctx->len], data, len);
	ctx->len += len;

	return 0;
}

static int bypass_consume_base64(const struct shell *sh, struct bypass_ctx *ctx,
				 uint8_t *data, size_t len)
{
	uint8_t *orig_data = data;
	size_t orig_len = len;
	uint8_t *data_end = &data[len];
	size_t olen;
	int err;

	while (data < data_end) {
		void *old_data = ctx->data;
		size_t copy_len = MIN(sizeof(ctx->base64_buf) - ctx->base64_buf_len,
				      data_end - data);

		memcpy(&ctx->base64_buf[ctx->base64_buf_len], data, copy_len);
		ctx->base64_buf_len += copy_len;
		data += copy_len;

		if (ctx->base64_buf_len < sizeof(ctx->base64_buf)) {
			/* Not enough data to process */
			break;
		}

		ctx->data = realloc(old_data, ctx->len + 3);
		if (!ctx->data) {
			free(old_data);
			ctx->len = 0;
			shell_error(sh, "Not enough memory");
			return -ENOMEM;
		}

		err = base64_decode(&ctx->data[ctx->len], 3, &olen,
				    ctx->base64_buf, sizeof(ctx->base64_buf));
		if (err) {
			shell_error(sh, "Failed to decode base64: %d", err);
			shell_hexdump(sh, ctx->base64_buf, sizeof(ctx->base64_buf));
			shell_hexdump(sh, &ctx->data[ctx->len], 3);
			shell_error(sh, "ctx->len %zu", (size_t) ctx->len);
			shell_error(sh, "data, len dump");
			shell_hexdump(sh, orig_data, orig_len);
			free(ctx->data);
			ctx->len = 0;
			return err;
		}

		ctx->base64_buf_len = 0;
		ctx->len += olen;
	}

	return 0;
}

static void bypass_finish(const struct shell *sh, struct bypass_ctx *ctx)
{
	free(ctx->data);
	ctx->data = NULL;

	ctx->len = 0;

	shell_print(sh, ""); /* newline */
	shell_set_bypass(sh, NULL);
}

static void bypass_cb(const struct shell *sh, uint8_t *data, size_t len)
{
	struct bypass_ctx *ctx = &shell_bypass_context;
	bool end_of_input = false;
	int err;

	for (uint8_t *p = data; p < &data[len]; p++) {
		if (*p == SHELL_VT100_ASCII_CTRL_D) {
			size_t processed_len = p - data;

			/* This char is not part of data, so adjust for that */
			len -= 1;

			if (processed_len < len) {
				shell_warn(sh, "Discarding %zu bytes following Ctrl+D",
					   (len - processed_len));
				len = processed_len;
			}

			end_of_input = true;

			break;
		}

		/* Echo received characters */
		shell_fprintf(sh, SHELL_NORMAL, "%c", *p);
	}

	switch (ctx->content_format) {
	case BYPASS_CONTENT_FORMAT_TEXT:
		err = bypass_consume_text(sh, ctx, data, len);
		break;
	case BYPASS_CONTENT_FORMAT_BASE64:
		err = bypass_consume_base64(sh, ctx, data, len);
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err) {
		bypass_finish(sh, ctx);
		return;
	}

	if (end_of_input) {
		err = tls_credential_add(ctx->tag, ctx->type, ctx->data, ctx->len);
		if (err) {
			shell_error(sh, "Failed to add credential: %d", err);
			free(ctx->data);
		}

		/*
		 * FIXME: this leaks heap area allocated for TLS credential. Such area should be
		 * tracked, so that next time TLS credential with the same tag and type gets
		 * assigned, old area can be safely deallocated.
		 */
		ctx->data = NULL;

		bypass_finish(sh, ctx);
	}
}

static int bypass_content_type_from_str(const char *str,
					enum bypass_content_format *content_format)
{
	for (const struct bypass_content_format_info *info = bypass_content_formats;
	     info < &bypass_content_formats[ARRAY_SIZE(bypass_content_formats)];
	     info++) {
		if (!strcmp(info->name, str)) {
			*content_format = info->content_format;
			return 0;
		}
	}

	return -EINVAL;
}

static int cmd_tls_credential_add_parse_argv_options(const struct shell *sh,
						     size_t *argc, char ***argv,
						     enum bypass_content_format *content_format)
{
	struct getopt_state *state;
	int err;
	int c;

	while ((c = getopt(*argc, *argv, "t:")) != -1) {
		state = getopt_state_get();
		switch (c) {
		case 't':
			err = bypass_content_type_from_str(state->optarg,
							   content_format);
			if (err) {
				return err;
			}
			break;
		case 'h':
			/* When getopt is active shell is not parsing
			 * command handler to print help message. It must
			 * be done explicitly.
			 */
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		default:
			shell_help(sh);
			return SHELL_CMD_HELP_PRINTED;
		}
	}

	state = getopt_state_get();

	*argv += state->optind;
	*argc -= state->optind;

	return 0;
}

static int cmd_tls_credential_pasrse_tag_and_type(const struct shell *sh,
						  char **argv,
						  long *tag,
						  enum tls_credential_type *type)
{
	const char *tag_s = argv[0];
	const char *type_s = argv[1];

	*tag = strtol(tag_s, NULL, 0);

	*type = tls_credential_str_to_type(type_s);
	if (*type == TLS_CREDENTIAL_NONE) {
		shell_error(sh, "Invalid credential type specified (%s)", type_s);
		return -EINVAL;
	}

	return 0;
}

static int cmd_tls_credential_add_parse_argv_positional(const struct shell *sh,
							size_t argc, char **argv,
							long *tag,
							enum tls_credential_type *type,
							void **value, size_t *value_len)
{
	int err;

	err = cmd_tls_credential_pasrse_tag_and_type(sh, argv, tag, type);
	if (err) {
		return err;
	}

	*value = NULL;

	if (argc > 2) {
		/* Parse credential directly from argument (handy for PSK-ID/PSK) */
		const void *val = argv[2];
		size_t len = strlen(val);

		*value = malloc(len);
		if (!(*value)) {
			shell_error(sh, "Not enough memory");
			return -ENOMEM;
		}

		memcpy(*value, val, len);
		*value_len = len;
	}

	return 0;
}

static int cmd_tls_credential_add(const struct shell *sh, size_t argc, char **argv)
{
	long tag;
	enum tls_credential_type type;
	void *value;
	size_t value_len;
	enum bypass_content_format content_format = BYPASS_CONTENT_FORMAT_TEXT;
	int ret;
	int err;

	ret = cmd_tls_credential_add_parse_argv_options(sh, &argc, &argv, &content_format);
	if (ret != 0) {
		return ret;
	}

	if (argc < 2) {
		shell_error(sh, "wrong parameter count");
		return -EINVAL;
	}

	ret = cmd_tls_credential_add_parse_argv_positional(sh, argc, argv,
							   &tag, &type,
							   &value, &value_len);
	if (ret != 0) {
		return ret;
	}

	if (value) {
		/*
		 * FIXME: this leaks heap area allocated for TLS credential (stored in 'value').
		 * Such area should be tracked, so that next time TLS credential with the same tag
		 * and type gets assigned, old area can be safely deallocated.
		 */
		err = tls_credential_add(tag, type, value, value_len);
		if (err) {
			shell_error(sh, "Failed to add credential: %d", err);
		}
		return err;
	}

	/* Parse from shell input */
	shell_bypass_context.tag = tag;
	shell_bypass_context.type = type;
	shell_bypass_context.content_format = content_format;
	shell_bypass_context.base64_buf_len = 0;
	shell_set_bypass(sh, bypass_cb);

	return 0;
}

static int cmd_tls_credential_delete(const struct shell *sh, size_t argc, char **argv)
{
	long tag;
	enum tls_credential_type type;
	int err;

	err = cmd_tls_credential_pasrse_tag_and_type(sh, &argv[1], &tag, &type);
	if (err) {
		return err;
	}

	err = tls_credential_delete(tag, type);
	if (err) {
		shell_error(sh, "Failed to delete credential: %d", err);
		return err;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(cmds_tls_credential,
	SHELL_CMD(add, NULL,
		"add [-t text|base64] <tag> <type> [value]",
		cmd_tls_credential_add),
	SHELL_CMD_ARG(delete, NULL,
		"delete <tag> <type>",
		cmd_tls_credential_delete, 3, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(tls_credential, &cmds_tls_credential, "TLS credential shell", NULL);
