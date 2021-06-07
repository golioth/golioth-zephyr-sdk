/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(golioth_logging, LOG_LEVEL_DBG);

#include <net/coap.h>
#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_writer.h>
#include <net/golioth/system_client.h>
#include <net/golioth/wifi.h>

#include "gps.h"

#define GOLIOTH_LIGHTDB_STREAM_PATH(x) ".s/" x

static struct golioth_client *client = GOLIOTH_SYSTEM_CLIENT_GET();

static void golioth_on_message(struct golioth_client *client,
                   struct coap_packet *rx)
{
    uint16_t payload_len;
    const uint8_t *payload;
    uint8_t type;

    type = coap_header_get_type(rx);
    payload = coap_packet_get_payload(rx, &payload_len);

    if (!IS_ENABLED(CONFIG_LOG_BACKEND_GOLIOTH) && payload) {
        LOG_HEXDUMP_DBG(payload, payload_len, "Payload");
    }
}

static void print_gps_fix_data(nrf_gnss_data_frame_t *gps_data)
{
    printk("Longitude:  %f\n", gps_data->pvt.longitude);
    printk("Latitude:   %f\n", gps_data->pvt.latitude);
    printk("Altitude:   %f\n", gps_data->pvt.altitude);
    printk("Speed:      %f\n", gps_data->pvt.speed);
    printk("Heading:    %f\n", gps_data->pvt.heading);
}

static void upload_gps_fix_data(nrf_gnss_data_frame_t *gps_data)
{
    uint8_t buf[128];
    struct cbor_buf_writer buf_writer;
    CborEncoder encoder, map_encoder;
    CborError cbor_err = CborNoError;
    int golioth_err;

    cbor_buf_writer_init(&buf_writer, buf, sizeof(buf));
    cbor_encoder_init(&encoder, &buf_writer.enc, 0);

    cbor_err |= cbor_encoder_create_map(&encoder, &map_encoder, 1);

    cbor_err |= cbor_encode_text_stringz(&map_encoder, "longitude");
    cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.longitude);

    cbor_err |= cbor_encode_text_stringz(&map_encoder, "latitude");
    cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.latitude);

    cbor_err |= cbor_encode_text_stringz(&map_encoder, "altitude");
    cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.altitude);

    cbor_err |= cbor_encode_text_stringz(&map_encoder, "speed");
    cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.speed);

    cbor_err |= cbor_encode_text_stringz(&map_encoder, "heading");
    cbor_err |= cbor_encode_float(&map_encoder, gps_data->pvt.heading);

    cbor_err |= cbor_encoder_close_container(&encoder, &map_encoder);

    // If any errors other than `CborOutOfMemory` occured, abort.
    if ((cbor_err & ~(CborNoError | CborErrorOutOfMemory)) != 0) {
        LOG_ERR("failed to generate gps fix cbor data");
        return;
    }

    if ((cbor_err & CborErrorOutOfMemory) != 0) {
        LOG_WRN("the buffer is too small to cbor encode the GPS fix.");
        return;
    }

    // Push the GPS fix data to a LightDB stream called `gps`.
    golioth_err = golioth_lightdb_set(
        client,
        GOLIOTH_LIGHTDB_STREAM_PATH("gps"),
        COAP_CONTENT_FORMAT_APP_CBOR,
        buf, cbor_buf_writer_buffer_size(&buf_writer, buf)
    );

    if (golioth_err) {
        LOG_ERR("Failed to push GPS fix data to stream: %d", golioth_err);
    }
}

void main(void)
{
    nrf_gnss_data_frame_t gps_data;

    LOG_INF("Starting GPS Sample...");

    if (gps_init() != 0) {
        LOG_ERR("Failed to init GPS");
    }

    if (IS_ENABLED(CONFIG_GOLIOTH_SAMPLE_WIFI)) {
        LOG_INF("Connecting to WiFi");
        wifi_connect();
    }

    client->on_message = golioth_on_message;
    golioth_system_client_start();

    while (true) {
        // Loop until we don't have any more data to read.
        while (gps_process_data(&gps_data) > 0) {}
        
        if (gps_has_fix()) {
            print_gps_fix_data(&gps_data);
            upload_gps_fix_data(&gps_data);
        } else {
            printk("No gps data available\n");
            printk("Seconds since last GPS fix: %lld\n", gps_msec_since_last_fix() / 1000);
        }

        k_sleep(K_SECONDS(5));
    }
}