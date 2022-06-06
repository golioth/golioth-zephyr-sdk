/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

struct settings_read_callback_params {
	const struct shell *shell_ptr;
	bool value_found;
	bool json_output;
};

static int cmd_settings_set(const struct shell *shell, size_t argc, char *argv[])
{
	bool json_output;

	if (argc < 3) {
		shell_fprintf(shell, SHELL_WARNING,
			      "Wrong number of arguments\n");
		shell_help(shell);
		return -ENOEXEC;
	}

	json_output = false;
	if (argc >= 4) {
		if (strcmp(argv[3], "--json") == 0) {
			json_output = true;
		}
	}

	const char *name = argv[1];
	const char *val = argv[2];
	size_t val_len = strlen(val);

	int err;

	shell_fprintf(shell, SHELL_NORMAL, "Setting %s to %s\n", name, val);

#ifdef CONFIG_SETTINGS_RUNTIME
	err = settings_runtime_set(name, val, val_len);
	if (err) {
		if (json_output) {
			shell_fprintf(shell, SHELL_VT100_COLOR_RED,
				"{\"status\": \"failed\", "
				"\"msg\": \"Failed to set runtime setting: %s:%s\"}\n", name, val);
		} else {
			shell_fprintf(shell, SHELL_ERROR,
				      "Failed to set runtime setting: %s:%s\n", name, val);
		}

		return -ENOEXEC;
	}
#endif

	err = settings_save_one(name, val, val_len);
	if (err) {
		if (json_output) {
			shell_fprintf(shell, SHELL_VT100_COLOR_RED,
				"{\"status\": \"failed\", "
				"\"msg\": \"failed to save setting %s:%s\"}\n", name, val);
		} else {
			shell_fprintf(shell, SHELL_ERROR,
				      "Failed to save setting %s:%s\n", name, val);
		}

		return -ENOEXEC;
	}

	if (json_output) {
		shell_fprintf(shell, SHELL_VT100_COLOR_GREEN,
			"{\"status\": \"success\", \"msg\": \"setting %s saved as %s\"}\n",
			name, val);
	} else {
		shell_fprintf(shell, SHELL_NORMAL,
			      "Setting %s saved as %s\n", name, val);
	}

	return 0;
}

static int settings_read_callback(const char *key,
				  size_t len,
				  settings_read_cb read_cb,
				  void            *cb_arg,
				  void            *param)
{
	ssize_t num_read_bytes = MIN(len, SETTINGS_MAX_VAL_LEN);
	uint8_t buffer[num_read_bytes + 1];
	struct settings_read_callback_params *params = param;

	/* Process only the exact match and ignore descendants of the searched name */
	if (settings_name_next(key, NULL) != 0) {
		return 0;
	}

	params->value_found = true;
	num_read_bytes = read_cb(cb_arg, buffer, num_read_bytes);

	if (num_read_bytes < 0) {
		if (params->json_output) {
			shell_fprintf(params->shell_ptr, SHELL_VT100_COLOR_RED,
				"{\"status\": \"failed\", "
				"\"msg\": \"failed to read value: %d\"}\n",
				(int)num_read_bytes);
		} else {
			shell_error(params->shell_ptr, "Failed to read value: %d",
				(int) num_read_bytes);
		}

		return 0;
	}

	/*  add NULL to the last position in the buffer */
	buffer[num_read_bytes] = 0x00;

	if (params->json_output) {
		shell_fprintf(params->shell_ptr, SHELL_VT100_COLOR_GREEN,
			"{\"status\": \"success\", \"value\": \"%s\"}\n", buffer);
	} else {
		shell_fprintf(params->shell_ptr, SHELL_VT100_COLOR_GREEN, "%s\n", buffer);
	}

	if (len > SETTINGS_MAX_VAL_LEN) {
		shell_print(params->shell_ptr, "(The output has been truncated)");
	}

	return 0;
}

static int cmd_settings_get(const struct shell *shell, size_t argc,
			    char *argv[])
{
	bool json_output;

	int err;
	const char *name = argv[1];

	json_output = false;
	if (argc >= 3) {
		if (strcmp(argv[2], "--json") == 0) {
			json_output = true;
		}
	}

	struct settings_read_callback_params params = {
		.shell_ptr = shell,
		.value_found = false,
		.json_output = json_output
	};

	err = settings_load_subtree_direct(name, settings_read_callback, &params);

	if (err) {
		if (json_output) {
			shell_fprintf(shell, SHELL_VT100_COLOR_RED,
				"{\"status\": \"failed\", "
				"\"msg\": \"failed to load settings: %d\"}\n", err);
		} else {
			shell_error(shell, "Failed to load settings: %d", err);
		}
	} else if (!params.value_found) {
		if (json_output) {
			shell_fprintf(shell, SHELL_VT100_COLOR_RED,
				"{\"status\": \"failed\", \"msg\": \"setting not found\"}\n");
		} else {
			shell_error(shell, "Setting not found");
		}
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(settings_commands,
	SHELL_CMD_ARG(set, NULL,
		"set a module config\n"
		"usage:\n"
		"$ settings set <key> <value>\n"
		"example:\n"
		"$ settings set golioth/psk pre-shared-key-value\n"
		"$ settings set golioth/psk-id psk-identity-value",
		cmd_settings_set, 3, 1),
	SHELL_CMD_ARG(get, NULL,
		"get a module config value\n"
		"usage:\n"
		"$ settings get <key>\n"
		"example:\n"
		"$ settings get golioth/psk\n"
		"$ settings get golioth/psk-id",
		cmd_settings_get, 2, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(settings, &settings_commands, "Settings commands", NULL);
