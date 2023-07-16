/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_hello, LOG_LEVEL_DBG);

#include <net/golioth/system_client.h>
#include <samples/common/net_connect.h>
#include <zephyr/net/coap.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>

#define CLIENT_CERTIFICATE_PATH "/lfs1/credentials/client_cert.der"
#define PRIVATE_KEY_PATH        "/lfs1/credentials/private_key.der"

#define STORAGE_PARTITION_LABEL	storage_partition
#define STORAGE_PARTITION_ID	FIXED_PARTITION_ID(STORAGE_PARTITION_LABEL)

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(cstorage);
static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &cstorage,
	.storage_dev = (void *)STORAGE_PARTITION_ID,
	.mnt_point = "/lfs1"
};

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static K_SEM_DEFINE(connected, 0, 1);

static void golioth_on_connect(struct golioth_client *client)
{
	k_sem_give(&connected);
}

static int load_credential_from_fs(const char *path, enum tls_credential_type type)
{
	struct fs_file_t file;
	struct fs_dirent dirent;

	fs_file_t_init(&file);

	int err = fs_stat(path, &dirent);

	if (err < 0) {
		LOG_WRN("Could not stat %s, err: %d", path, err);
		goto finish;
	}
	if (dirent.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("%s is not a file", path);
		err = -EISDIR;
		goto finish;
	}
	if (dirent.size == 0) {
		LOG_ERR("%s is an empty file", path);
		err = -EINVAL;
		goto finish;
	}


	err = fs_open(&file, path, FS_O_READ);

	if (err < 0) {
		LOG_ERR("Could not open %s", path);
		goto finish;
	}

	/* NOTE: cred_buf is used directly by the TLS Credentials library, and so must remain
	 * allocated for the life of the program.
	 */

	void *cred_buf = malloc(dirent.size);

	if (cred_buf == NULL) {
		LOG_ERR("Could not allocate space to read credential");
		err = -ENOMEM;
		goto finish_with_file;
	}

	err = fs_read(&file, cred_buf, dirent.size);

	if (err < 0) {
		LOG_ERR("Could not read %s, err: %d", path, err);
		free(cred_buf);
		goto finish_with_file;
	}

	LOG_INF("Read %d bytes from %s", err, path);

	/* Write to credential store */

	err = tls_credential_add(CONFIG_GOLIOTH_SYSTEM_CLIENT_CREDENTIALS_TAG,
				 type,
				 cred_buf, err);

	if (err < 0) {
		LOG_ERR("Could not load credential, err: %d", err);
		free(cred_buf);
	}

finish_with_file:
	fs_close(&file);

finish:
	return err;
}

int main(void)
{
	int err;

	LOG_DBG("Start certificate provisioning sample");

	err = fs_mount(&littlefs_mnt);
	if (err < 0) {
		LOG_ERR("Error mounting littlefs [%d]", err);
	}

	load_credential_from_fs(CLIENT_CERTIFICATE_PATH, TLS_CREDENTIAL_SERVER_CERTIFICATE);
	load_credential_from_fs(PRIVATE_KEY_PATH, TLS_CREDENTIAL_PRIVATE_KEY);

	if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLES_COMMON)) {
		net_connect();
	}

	client->on_connect = golioth_on_connect;
	golioth_system_client_start();

	k_sem_take(&connected, K_FOREVER);

	int counter = 0;

	while (true) {
		k_sleep(K_SECONDS(5));
		LOG_INF("Sending hello! %d", counter);
		++counter;
	}

	return 0;
}
