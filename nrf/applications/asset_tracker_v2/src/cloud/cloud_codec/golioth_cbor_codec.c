/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cloud_codec.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <date_time.h>

#include <tinycbor/cbor.h>
#include <tinycbor/cbor_buf_reader.h>
#include <tinycbor/cbor_buf_writer.h>

#include "json_protocol_names.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CLOUD_CODEC_LOG_LEVEL);

struct config_value {
	const char *name;
	size_t value_offset;
	int (*apply)(CborValue *value, void *data);
};

static int apply_boolean(CborValue *value, void *data)
{
	bool *d = data;

	if (!cbor_value_is_boolean(value)) {
		return -EINVAL;
	}

	cbor_value_get_boolean(value, d);

	return 0;
}

static int apply_int(CborValue *value, void *data)
{
	int *d = data;

	switch (cbor_value_get_type(value)) {
	case CborIntegerType: {
		cbor_value_get_int(value, d);
		break;
	}
	case CborFloatType: {
		float v;
		cbor_value_get_float(value, &v);
		*d = v;
		break;
	}
	case CborDoubleType: {
		double v;
		cbor_value_get_double(value, &v);
		*d = v;
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int apply_double(CborValue *value, void *data)
{
	double *d = data;

	switch (cbor_value_get_type(value)) {
	case CborIntegerType: {
		int v;
		cbor_value_get_int(value, &v);
		*d = v;
		break;
	}
	case CborFloatType: {
		float v;
		cbor_value_get_float(value, &v);
		*d = v;
		break;
	}
	case CborDoubleType:
		cbor_value_get_double(value, d);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define CONFIG_VALUE(_name, _member, _apply)				\
	{								\
		.name = _name,						\
		.value_offset = offsetof(struct cloud_data_cfg, _member), \
		.apply = _apply,					\
	}

static const struct config_value config_values[] = {
	CONFIG_VALUE(CONFIG_GPS_TIMEOUT, gps_timeout, apply_int),
	CONFIG_VALUE(CONFIG_DEVICE_MODE, active_mode, apply_boolean),
	CONFIG_VALUE(CONFIG_ACTIVE_TIMEOUT, active_wait_timeout, apply_int),
	CONFIG_VALUE(CONFIG_MOVE_RES, movement_resolution, apply_int),
	CONFIG_VALUE(CONFIG_MOVE_TIMEOUT, movement_timeout, apply_int),
	CONFIG_VALUE(CONFIG_ACC_THRESHOLD, accelerometer_threshold, apply_double),
};

static int config_cbor_field_apply(CborValue *value, struct cloud_data_cfg *data)
{
	const struct config_value *cv;
	CborError cbor_err;
	bool equal;
	int err;

	for (cv = config_values; cv < &config_values[ARRAY_SIZE(config_values)]; cv++) {
		cbor_err = cbor_value_text_string_equals(value, cv->name, &equal);
		if (cbor_err != CborNoError) {
			return -EINVAL;
		}

		if (!equal) {
			continue;
		}

		cbor_value_advance(value);

		err = cv->apply(value, (uint8_t *) data + cv->value_offset);
		if (err) {
			LOG_WRN("Failed to apply %s", cv->name);
		}

		cbor_value_advance_fixed(value);
	}

	return 0;
}

int cloud_codec_encode_neighbor_cells(struct cloud_codec_data *output,
				      struct cloud_data_neighbor_cells *neighbor_cells)
{
	__ASSERT_NO_MSG(output != NULL);
	__ASSERT_NO_MSG(neighbor_cells != NULL);

	neighbor_cells->queued = false;
	return -ENOTSUP;
}

int cloud_codec_decode_config(char *input, size_t input_len,
			      struct cloud_data_cfg *data)
{
	struct cbor_buf_reader reader;
	CborParser parser;
	CborValue value;
	CborValue map;
	CborError cbor_err;
	int err = 0;

	if (input == NULL) {
		return -EINVAL;
	}

	cbor_buf_reader_init(&reader, input, input_len);
	cbor_err = cbor_parser_init(&reader.r, 0, &parser, &value);
	if (cbor_err != CborNoError) {
		LOG_ERR("Failed to init CBOR parser: %d", cbor_err);
		return -EINVAL;
	}

	/* Verify that the incoming CBOR message is a map. */
	if (cbor_value_is_null(&value)) {
		return -ENODATA;
	}

	if (!cbor_value_is_map(&value)) {
		return -ENOENT;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		LOG_HEXDUMP_DBG(input, input_len, "input");
	}

	cbor_err = cbor_value_enter_container(&value, &map);
	if (cbor_err != CborNoError) {
		LOG_WRN("Failed to enter map: %d", err);
		return -EINVAL;
	}

	if (cbor_value_at_end(&map)) {
		return -ENODATA;
	}

	while (!cbor_value_at_end(&map)) {
		cbor_err = config_cbor_field_apply(&map, data);
		if (cbor_err) {
			return -ENOMEM;
		}
	}

	cbor_err = cbor_value_leave_container(&value, &map);
	if (cbor_err != CborNoError) {
		LOG_WRN("Failed to leave map: %d", cbor_err);
		err = -ENOMEM;
	}

	return err;
}

static int cloud_codec_encode_config_subtree(CborEncoder *enc,
					     struct cloud_data_cfg *data)
{
	CborEncoder map;

	cbor_encoder_create_map(enc, &map, ARRAY_SIZE(config_values));

	cbor_encode_text_stringz(&map, CONFIG_GPS_TIMEOUT);
	cbor_encode_int(&map, data->gps_timeout);

	cbor_encode_text_stringz(&map, CONFIG_DEVICE_MODE);
	cbor_encode_boolean(&map, data->active_mode);

	cbor_encode_text_stringz(&map, CONFIG_ACTIVE_TIMEOUT);
	cbor_encode_int(&map, data->active_wait_timeout);

	cbor_encode_text_stringz(&map, CONFIG_MOVE_RES);
	cbor_encode_int(&map, data->movement_resolution);

	cbor_encode_text_stringz(&map, CONFIG_MOVE_TIMEOUT);
	cbor_encode_int(&map, data->movement_timeout);

	cbor_encode_text_stringz(&map, CONFIG_ACC_THRESHOLD);
	cbor_encode_double(&map, data->accelerometer_threshold);

	cbor_encoder_close_container(enc, &map);

	return 0;
}

int cloud_codec_encode_config(struct cloud_codec_data *output,
			      struct cloud_data_cfg *data)
{
	struct cbor_buf_writer writer;
	CborEncoder encoder;
	CborEncoder map;
	uint8_t *buffer;

	buffer = k_malloc(256);
	if (!buffer) {
		return -ENOMEM;
	}

	cbor_buf_writer_init(&writer, buffer, 256);
	cbor_encoder_init(&encoder, &writer.enc, 0);

	cbor_encoder_create_map(&encoder, &map, 1);

	cbor_encode_text_stringz(&map, DATA_CONFIG);
	cloud_codec_encode_config_subtree(&map, data);

	cbor_encoder_close_container(&encoder, &map);

	output->buf = buffer;
	output->len = writer.enc.bytes_written;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		LOG_HEXDUMP_DBG(buffer, writer.enc.bytes_written, "encoded config");
	}

	return 0;
}

static int cbor_encode_ui_data(CborEncoder *enc, struct cloud_data_ui *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->btn_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->btn_ts);

	cbor_encode_text_stringz(enc, DATA_BUTTON);
	cbor_encode_uint(&map, data->btn);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_modem_static_value(CborEncoder *enc,
					  struct cloud_data_modem_static *data)
{
	char nw_mode[50] = {0};
	CborEncoder map;

	static const char lte_string[] = "LTE-M";
	static const char nbiot_string[] = "NB-IoT";
	static const char gps_string[] = " GPS";

	/* encode 'nw_mode' */
	if (data->nw_lte_m) {
		strcpy(nw_mode, lte_string);
	} else if (data->nw_nb_iot) {
		strcpy(nw_mode, nbiot_string);
	}

	if (data->nw_gps) {
		strcat(nw_mode, gps_string);
	}

	/* create CBOR map */
	cbor_encoder_create_map(enc, &map, CborIndefiniteLength);

	cbor_encode_text_stringz(&map, MODEM_CURRENT_BAND);
	cbor_encode_uint(&map, data->bnd);

	cbor_encode_text_stringz(&map, MODEM_NETWORK_MODE);
	cbor_encode_text_stringz(&map, nw_mode);

	cbor_encode_text_stringz(&map, MODEM_ICCID);
	cbor_encode_text_stringz(&map, data->iccid);

	cbor_encode_text_stringz(&map, MODEM_FIRMWARE_VERSION);
	cbor_encode_text_stringz(&map, data->fw);

	cbor_encode_text_stringz(&map, MODEM_BOARD);
	cbor_encode_text_stringz(&map, data->brdv);

	cbor_encode_text_stringz(&map, MODEM_APP_VERSION);
	cbor_encode_text_stringz(&map, data->appv);

	cbor_encoder_close_container(enc, &map);

	return 0;
}

static int cbor_encode_modem_static_data(CborEncoder *enc,
					 struct cloud_data_modem_static *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->ts);

	cbor_encode_text_stringz(enc, DATA_MODEM_STATIC);
	cbor_encode_modem_static_value(&map, data);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_modem_dynamic_value(CborEncoder *enc,
					   struct cloud_data_modem_dynamic *data)
{
	uint32_t mccmnc;
	char *end_ptr;
	CborEncoder map;

	cbor_encoder_create_map(enc, &map, CborIndefiniteLength);

	if (data->rsrp_fresh) {
		cbor_encode_text_stringz(&map, MODEM_RSRP);
		cbor_encode_int(&map, data->rsrp);
	}

	if (data->area_code_fresh) {
		cbor_encode_text_stringz(&map, MODEM_AREA_CODE);
		cbor_encode_uint(&map, data->area);
	}

	if (data->mccmnc_fresh) {
		/* Convert mccmnc to unsigned long integer. */
		errno = 0;
		mccmnc = strtoul(data->mccmnc, &end_ptr, 10);

		if ((errno == ERANGE) || (*end_ptr != '\0')) {
			LOG_ERR("MCCMNC string could not be converted.");
			return -ENOTEMPTY;
		}

		cbor_encode_text_stringz(&map, MODEM_MCCMNC);
		cbor_encode_uint(&map, mccmnc);
	}

	if (data->cell_id_fresh) {
		cbor_encode_text_stringz(&map, MODEM_CELL_ID);
		cbor_encode_uint(&map, data->cell);
	}

	if (data->ip_address_fresh) {
		cbor_encode_text_stringz(&map, MODEM_IP_ADDRESS);
		cbor_encode_text_stringz(&map, data->ip);
	}

	cbor_encoder_close_container(enc, &map);

	return 0;
}

static int cbor_encode_modem_dynamic_data(CborEncoder *enc,
					 struct cloud_data_modem_dynamic *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->ts);

	cbor_encode_text_stringz(enc, DATA_MODEM_DYNAMIC);
	cbor_encode_modem_dynamic_value(&map, data);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_gps_value_pvt(CborEncoder *enc,
				     struct cloud_data_gps *data)
{
	CborEncoder map;

	cbor_encoder_create_map(enc, &map, CborIndefiniteLength);

	cbor_encode_text_stringz(&map, DATA_GPS_LONGITUDE);
	cbor_encode_int(&map, data->pvt.longi);

	cbor_encode_text_stringz(&map, DATA_GPS_LATITUDE);
	cbor_encode_int(&map, data->pvt.lat);

	cbor_encode_text_stringz(&map, DATA_MOVEMENT);
	cbor_encode_int(&map, data->pvt.acc);

	cbor_encode_text_stringz(&map, DATA_GPS_ALTITUDE);
	cbor_encode_int(&map, data->pvt.alt);

	cbor_encode_text_stringz(&map, DATA_GPS_SPEED);
	cbor_encode_int(&map, data->pvt.spd);

	cbor_encode_text_stringz(&map, DATA_GPS_HEADING);
	cbor_encode_int(&map, data->pvt.hdg);

	cbor_encoder_close_container(enc, &map);

	return 0;
}

static int cbor_encode_gps_value_nmea(CborEncoder *enc,
				      struct cloud_data_gps *data)
{
	cbor_encode_text_stringz(enc, data->nmea);

	return 0;
}

static int cbor_encode_gps_data(CborEncoder *enc, struct cloud_data_gps *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->gps_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->gps_ts);

	cbor_encode_text_stringz(enc, DATA_GPS);

	switch (data->format) {
	case CLOUD_CODEC_GPS_FORMAT_PVT:
		cbor_encode_gps_value_pvt(&map, data);
		break;
	case CLOUD_CODEC_GPS_FORMAT_NMEA:
		cbor_encode_gps_value_nmea(&map, data);
		break;
	default:
		LOG_WRN("GPS data format not set");
		return -EINVAL;
	}

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_sensor_value(CborEncoder *enc,
				    struct cloud_data_sensors *data)
{
	CborEncoder map;

	cbor_encoder_create_map(enc, &map, CborIndefiniteLength);

	cbor_encode_text_stringz(&map, DATA_TEMPERATURE);
	cbor_encode_double(&map, data->temp);

	cbor_encode_text_stringz(&map, DATA_HUMID);
	cbor_encode_double(&map, data->hum);

	cbor_encoder_close_container(enc, &map);

	return 0;
}

static int cbor_encode_sensor_data(CborEncoder *enc,
				   struct cloud_data_sensors *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->env_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->env_ts);

	cbor_encode_text_stringz(enc, DATA_ENVIRONMENTALS);
	cbor_encode_sensor_value(&map, data);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_accel_value(CborEncoder *enc,
				   struct cloud_data_accelerometer *data)
{
	CborEncoder map;

	cbor_encoder_create_map(enc, &map, CborIndefiniteLength);

	cbor_encode_text_stringz(&map, DATA_MOVEMENT_X);
	cbor_encode_double(&map, data->values[0]);

	cbor_encode_text_stringz(&map, DATA_MOVEMENT_Y);
	cbor_encode_double(&map, data->values[1]);

	cbor_encode_text_stringz(&map, DATA_MOVEMENT_Z);
	cbor_encode_double(&map, data->values[2]);

	cbor_encoder_close_container(enc, &map);

	return 0;
}

static int cbor_encode_accel_data(CborEncoder *enc,
				  struct cloud_data_accelerometer *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->ts);

	cbor_encode_text_stringz(enc, DATA_MOVEMENT);
	cbor_encode_accel_value(&map, data);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

static int cbor_encode_battery_data(CborEncoder *enc,
				    struct cloud_data_battery *data)
{
	CborEncoder map;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->bat_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cbor_encoder_create_map(enc, &map, 2);

	cbor_encode_text_stringz(&map, "t");
	cbor_encode_uint(&map, data->bat_ts);

	cbor_encode_text_stringz(enc, DATA_BATTERY);
	cbor_encode_int(&map, data->bat);

	cbor_encoder_close_container(enc, &map);

	data->queued = false;

	return 0;
}

int cloud_codec_encode_data(struct cloud_codec_data *output,
			    struct cloud_data_gps *gps_buf,
			    struct cloud_data_sensors *sensor_buf,
			    struct cloud_data_modem_static *modem_stat_buf,
			    struct cloud_data_modem_dynamic *modem_dyn_buf,
			    struct cloud_data_ui *ui_buf,
			    struct cloud_data_accelerometer *accel_buf,
			    struct cloud_data_battery *bat_buf)
{
	struct cbor_buf_writer writer;
	CborEncoder encoder;
	CborEncoder arr;
	uint8_t *buffer;
	int err;

	buffer = k_malloc(256);
	if (!buffer) {
		return -ENOMEM;
	}

	cbor_buf_writer_init(&writer, buffer, 256);
	cbor_encoder_init(&encoder, &writer.enc, 0);

	cbor_encoder_create_array(&encoder, &arr, CborIndefiniteLength);

	err = cbor_encode_ui_data(&arr, ui_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_modem_static_data(&arr, modem_stat_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_modem_dynamic_data(&arr, modem_dyn_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_gps_data(&arr, gps_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_sensor_data(&arr, sensor_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_accel_data(&arr, accel_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	err = cbor_encode_battery_data(&arr, bat_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	cbor_encoder_close_container(&encoder, &arr);

	output->buf = buffer;
	output->len = writer.enc.bytes_written;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		LOG_HEXDUMP_DBG(buffer, writer.enc.bytes_written, "encoded data");
	}

	return 0;

free_buffer:
	k_free(buffer);

	return err;
}

int cloud_codec_encode_ui_data(struct cloud_codec_data *output,
			       struct cloud_data_ui *ui_buf)
{
	struct cbor_buf_writer writer;
	CborEncoder encoder;
	uint8_t *buffer;
	int err;

	buffer = k_malloc(256);
	if (!buffer) {
		return -ENOMEM;
	}

	cbor_buf_writer_init(&writer, buffer, 256);
	cbor_encoder_init(&encoder, &writer.enc, 0);

	err = cbor_encode_ui_data(&encoder, ui_buf);
	if (err && err != -ENODATA) {
		goto free_buffer;
	}

	output->buf = buffer;
	output->len = writer.enc.bytes_written;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		LOG_HEXDUMP_DBG(buffer, writer.enc.bytes_written, "encoded ui");
	}

	return 0;

free_buffer:
	k_free(buffer);

	return err;
}

int cloud_codec_encode_batch_data(
				struct cloud_codec_data *output,
				struct cloud_data_gps *gps_buf,
				struct cloud_data_sensors *sensor_buf,
				struct cloud_data_modem_dynamic *modem_dyn_buf,
				struct cloud_data_ui *ui_buf,
				struct cloud_data_accelerometer *accel_buf,
				struct cloud_data_battery *bat_buf,
				size_t gps_buf_count,
				size_t sensor_buf_count,
				size_t modem_dyn_buf_count,
				size_t ui_buf_count,
				size_t accel_buf_count,
				size_t bat_buf_count)
{
	struct cbor_buf_writer writer;
	CborEncoder encoder;
	CborEncoder arr;
	uint8_t *buffer;
	int err = 0;
	size_t i;
	size_t bytes_written_before;

	buffer = k_malloc(256);
	if (!buffer) {
		return -ENOMEM;
	}

	cbor_buf_writer_init(&writer, buffer, 2048);
	cbor_encoder_init(&encoder, &writer.enc, 0);

	cbor_encoder_create_array(&encoder, &arr, CborIndefiniteLength);

	bytes_written_before = writer.enc.bytes_written;

#define FOREACH_BUF(_buf, _count, _encode)				\
	for (i = 0; i < _count; i++) {					\
		err = _encode(&arr, &_buf[i]);				\
		if (err && err != -ENODATA) {				\
			LOG_WRN("Error when encoding batch (%d)", err);	\
			goto free_buffer;				\
		}							\
	}

	FOREACH_BUF(ui_buf, ui_buf_count, cbor_encode_ui_data);
	FOREACH_BUF(modem_dyn_buf, modem_dyn_buf_count, cbor_encode_modem_dynamic_data);
	FOREACH_BUF(gps_buf, gps_buf_count, cbor_encode_gps_data);
	FOREACH_BUF(sensor_buf, sensor_buf_count, cbor_encode_sensor_data);
	FOREACH_BUF(accel_buf, accel_buf_count, cbor_encode_accel_data);
	FOREACH_BUF(bat_buf, bat_buf_count, cbor_encode_battery_data);

#undef FOREACH_BUF

	if (writer.enc.bytes_written == bytes_written_before) {
		/* No data was actually written */
		err = -ENODATA;
		goto free_buffer;
	}

	cbor_encoder_close_container(&encoder, &arr);

	output->buf = buffer;
	output->len = writer.enc.bytes_written;

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		LOG_HEXDUMP_DBG(buffer, writer.enc.bytes_written, "encoded batch data");
	}

	return 0;

free_buffer:
	k_free(buffer);

	return err;
}
