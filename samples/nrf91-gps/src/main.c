/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_logging, LOG_LEVEL_DBG);

#include <net/coap.h>
// #include <tinycbor/cbor.h>
// #include <tinycbor/cbor_buf_writer.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>
#include <kernel.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>

// #include "gps.h"

#include <drivers/gps.h>

// static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();
static const struct device *gps_device;

// static void print_gps_satellite_stats(nrf_gnss_data_frame_t *gps_data) {
//     uint8_t tracked, in_fix, unhealthy;

//     gps_satellite_stats(gps_data, &tracked, &in_fix, &unhealthy);

//     printk("Tracking: %d, Using: [%d/%d], Unhealthy: [%d/%d]\n", tracked, in_fix, tracked, unhealthy, tracked);
// }

// static void print_gps_fix_data(nrf_gnss_data_frame_t *gps_data)
// {
//     printk("Longitude:  %f\n", gps_data->pvt.longitude);
//     printk("Latitude:   %f\n", gps_data->pvt.latitude);
//     printk("Altitude:   %f\n", gps_data->pvt.altitude);
//     printk("Speed:      %f\n", gps_data->pvt.speed);
//     printk("Heading:    %f\n", gps_data->pvt.heading);
// }

// static void upload_gps_fix_data(nrf_gnss_data_frame_t *gps_data)
// {
//     uint8_t buf[128];
//     struct cbor_buf_writer buf_writer;
//     CborEncoder encoder, map_encoder;
//     CborError cbor_err = CborNoError;
//     int golioth_err;

//     cbor_buf_writer_init(&buf_writer, buf, sizeof(buf));
//     cbor_encoder_init(&encoder, &buf_writer.enc, 0);

//     cbor_err |= cbor_encoder_create_map(&encoder, &map_encoder, 1);

//     cbor_err |= cbor_encode_text_stringz(&map_encoder, "longitude");
//     cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.longitude);

//     cbor_err |= cbor_encode_text_stringz(&map_encoder, "latitude");
//     cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.latitude);

//     cbor_err |= cbor_encode_text_stringz(&map_encoder, "altitude");
//     cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.altitude);

//     cbor_err |= cbor_encode_text_stringz(&map_encoder, "speed");
//     cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.speed);

//     cbor_err |= cbor_encode_text_stringz(&map_encoder, "heading");
//     cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.heading);

//     cbor_err |= cbor_encoder_close_container(&encoder, &map_encoder);

//     // If any errors other than `CborOutOfMemory` occured, abort.
//     if ((cbor_err & ~(CborNoError | CborErrorOutOfMemory)) != 0) {
//         LOG_ERR("failed to generate gps fix cbor data");
//         return;
//     }

//     if ((cbor_err & CborErrorOutOfMemory) != 0) {
//         LOG_WRN("the buffer is too small to cbor encode the GPS fix.");
//         return;
//     }

//     // Push the GPS fix data to a LightDB stream called `gps`.
//     golioth_err = golioth_lightdb_set(
//         client,
//         GOLIOTH_LIGHTDB_STREAM_PATH("gps"),
//         COAP_CONTENT_FORMAT_APP_CBOR,
//         buf, cbor_buf_writer_buffer_size(&buf_writer, buf)
//     );

//     if (golioth_err) {
//         LOG_ERR("Failed to push GPS fix data to stream: %d", golioth_err);
//     }
// }

static const char status1[] = "+CEREG: 1";
static const char status2[] = "+CEREG:1";
static const char status3[] = "+CEREG: 5";
static const char status4[] = "+CEREG:5";

#define AT_ACTIVATE_LTE     "AT+CFUN=21"
#define AT_DEACTIVATE_LTE   "AT+CFUN=20"
#define AT_CMD_SIZE(x) (sizeof(x) - 1)

K_SEM_DEFINE(lte_ready, 0, 1);

static void wait_for_lte(void *context, const char *response)
{
	if (!memcmp(status1, response, AT_CMD_SIZE(status1)) ||
		!memcmp(status2, response, AT_CMD_SIZE(status2)) ||
		!memcmp(status3, response, AT_CMD_SIZE(status3)) ||
		!memcmp(status4, response, AT_CMD_SIZE(status4))) {
		k_sem_give(&lte_ready);
	}
}

static int activate_lte(bool activate)
{
	if (activate) {
		if (at_cmd_write(AT_ACTIVATE_LTE, NULL, 0, NULL) != 0) {
			return -1;
		}

		at_notif_register_handler(NULL, wait_for_lte);
		if (at_cmd_write("AT+CEREG=2", NULL, 0, NULL) != 0) {
			return -1;
		}

		k_sem_take(&lte_ready, K_FOREVER);

		at_notif_deregister_handler(NULL, wait_for_lte);
		if (at_cmd_write("AT+CEREG=0", NULL, 0, NULL) != 0) {
			return -1;
		}
	} else {
		if (at_cmd_write(AT_DEACTIVATE_LTE, NULL, 0, NULL) != 0) {
			return -1;
		}
	}

	return 0;
}

void gps_event_handler(const struct device *dev, struct gps_event *event)
{
    LOG_INF("handling GPS event: %d", event->type);
}

static int gps_controller_init(void)
{
    int err;

    gps_device = device_get_binding(CONFIG_GPS_DEV_NAME);
    if (gps_device == NULL) {
        LOG_ERR("failed to retrieve GPS device");
        return -ENODEV;
    }

    err = gps_init(gps_device, gps_event_handler);
    if (err) {
        LOG_ERR("failed to initialize GPS device, err: %d", err);
        return err;
    }

    LOG_INF("GPS initialized");

    return err;
}

static void gps_controller_start(void)
{
    int err;

    struct gps_config cfg = {
        .nav_mode = GPS_NAV_MODE_PERIODIC,
        .power_mode = GPS_POWER_MODE_DISABLED,
        .timeout = 360,
        .interval = 360 + 30,
        .priority = true,
    };

    if (gps_device == NULL) {
        LOG_ERR("GPS device is not initialized");
        return;
    }

    LOG_INF("Enabling PSM");

	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("PSM request failed, error: %d", err);
	} else {
		LOG_INF("PSM enabled");
	}

    err = gps_start(gps_device, &cfg);
    if (err) {
        LOG_ERR("failed to start GPS, err: %d", err);
        return;
    }

    LOG_INF("GPS started successfully");
}

void main(void)
{
    LOG_INF("Starting GPS Sample...");

    activate_lte(false);
    // lte_lc_normal();

    if (at_cmd_write("AT+CFUN=31", NULL, 0, NULL) != 0) {
        LOG_ERR("failed to activate GNSS");
    }
    
    if (lte_lc_init_and_connect()) {
        LOG_ERR("failed to initialize modem");
    }

    enum lte_lc_func_mode functional_mode;

    if (lte_lc_func_mode_get(&functional_mode)) {
        LOG_ERR("failed to get functional mode");
    }

    LOG_INF("lte_lc functional mode: %d", functional_mode);

    if (gps_controller_init()) {
        return;
    }

    // activate_lte(true);
    // activate_lte(true);

    if (lte_lc_func_mode_get(&functional_mode)) {
        LOG_ERR("failed to get functional mode");
    }

    LOG_INF("lte_lc functional mode: %d", functional_mode);

    gps_controller_start();

    // golioth_system_client_start();

    while (true) {
        
        // if (gps_get_data(&gps_data) != 0) {
        //     // LOG_WRN("failed to get GPS data");
        //     printk("failed to get gps data\n");
        // }

        // print_gps_satellite_stats(&gps_data);

        // if (gps_has_fix()) {
        //     print_gps_fix_data(&gps_data);
        //     // upload_gps_fix_data(&gps_data);
        // } else {
        //     printk("No gps data available\n");
        //     printk("Seconds since last GPS fix: %lld\n", gps_msec_since_last_fix() / 1000);
        // }

        k_sleep(K_SECONDS(5));
    }
}
