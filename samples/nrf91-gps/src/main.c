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

void gps_event_handler(const struct device *dev, struct gps_event *event)
{
    switch (event->type) {
        case GPS_EVT_PVT:
            LOG_INF("latitude: %f, longitude: %f", event->pvt.latitude, event->pvt.longitude);
            break;
        default:
            break;
    }
}

static int gps_controller_init(void)
{
    int err;

    gps_device = device_get_binding(CONFIG_GPS_DEVICE_NAME);
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

    if (gps_controller_init()) {
        return;
    }

    gps_controller_start();

    // golioth_system_client_start();

    while (true) {
        k_sleep(K_SECONDS(5));
    }
}
