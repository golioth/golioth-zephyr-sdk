/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <cloud_codec.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>
#include <date_time.h>

#include "json_helpers.h"
#include "json_common.h"
#include "json_protocol_names.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CLOUD_CODEC_LOG_LEVEL);

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
	int err = 0;
	cJSON *root_obj = NULL;

	if (input == NULL) {
		return -EINVAL;
	}

	root_obj = cJSON_ParseWithLength(input, input_len);
	if (root_obj == NULL) {
		return -ENOENT;
	}

	/* Verify that the incoming JSON string is an object. */
	if (cJSON_IsNull(root_obj)) {
		return -ENODATA;
	}

	if (!cJSON_IsObject(root_obj)) {
		return -ENOENT;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		json_print_obj("Decoded message:\n", root_obj);
	}

	json_common_config_get(root_obj, data);

	cJSON_Delete(root_obj);
	return err;
}

int cloud_codec_encode_config(struct cloud_codec_data *output,
			      struct cloud_data_cfg *data)
{
	int err;
	char *buffer;

	cJSON *obj = cJSON_CreateObject();

	if (obj == NULL) {
		cJSON_Delete(obj);
		return -ENOMEM;
	}

	err = json_common_config_add(obj, data, DATA_CONFIG);

	if (err) {
		goto delete_obj;
	}

	buffer = cJSON_PrintUnformatted(obj);
	if (buffer == NULL) {
		LOG_ERR("Failed to allocate memory for JSON string");

		err = -ENOMEM;
		goto delete_obj;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		json_print_obj("Encoded message:\n", obj);
	}

	output->buf = buffer;
	output->len = strlen(buffer);

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_ui_data(cJSON *parent, struct cloud_data_ui *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->btn_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->btn_ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_VALUE, data->btn);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_modem_static_value(cJSON *parent, struct cloud_data_modem_static *data)
{
	char nw_mode[50] = {0};
	cJSON *obj;
	int err;

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

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, MODEM_CURRENT_BAND, data->bnd);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_str(obj, MODEM_NETWORK_MODE, nw_mode);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_str(obj, MODEM_ICCID, data->iccid);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_str(obj, MODEM_FIRMWARE_VERSION, data->fw);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_str(obj, MODEM_BOARD, data->brdv);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_str(obj, MODEM_APP_VERSION, data->appv);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj(parent, DATA_MODEM_STATIC, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_modem_static_data(cJSON *parent, struct cloud_data_modem_static *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_encode_modem_static_value(obj, data);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_modem_dynamic_value(cJSON *parent, struct cloud_data_modem_dynamic *data)
{
	cJSON *obj;
	uint32_t mccmnc;
	char *end_ptr;
	bool values_added = false;
	int err = 0;

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	if (data->rsrp_fresh) {
		err = json_add_number(obj, MODEM_RSRP, data->rsrp);
		if (err) {
			LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
			goto delete_obj;
		}
		values_added = true;
	}

	if (data->area_code_fresh) {
		err = json_add_number(obj, MODEM_AREA_CODE, data->area);
		if (err) {
			LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
			goto delete_obj;
		}
		values_added = true;
	}

	if (data->mccmnc_fresh) {
		/* Convert mccmnc to unsigned long integer. */
		errno = 0;
		mccmnc = strtoul(data->mccmnc, &end_ptr, 10);

		if ((errno == ERANGE) || (*end_ptr != '\0')) {
			LOG_ERR("MCCMNC string could not be converted.");
			err = -ENOTEMPTY;
			goto delete_obj;
		}

		err = json_add_number(obj, MODEM_MCCMNC, mccmnc);
		if (err) {
			LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
			goto delete_obj;
		}
		values_added = true;
	}

	if (data->cell_id_fresh) {
		err = json_add_number(obj, MODEM_CELL_ID, data->cell);
		if (err) {
			LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
			goto delete_obj;
		}
		values_added = true;
	}

	if (data->ip_address_fresh) {
		err = json_add_str(obj, MODEM_IP_ADDRESS, data->ip);
		if (err) {
			LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
			goto delete_obj;
		}
		values_added = true;
	}

	data->queued = false;

	if (!values_added) {
		err = -ENODATA;
		LOG_WRN("No valid dynamic modem data values present, entry unqueued");
		goto delete_obj;
	}

	json_add_obj(parent, DATA_MODEM_DYNAMIC, obj);

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_modem_dynamic_data(cJSON *parent, struct cloud_data_modem_dynamic *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_encode_modem_dynamic_value(obj, data);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_gps_value_pvt(cJSON *parent, struct cloud_data_gps *data)
{
	cJSON *obj;
	int err;

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, DATA_GPS_LONGITUDE, data->pvt.longi);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_GPS_LATITUDE, data->pvt.lat);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_MOVEMENT, data->pvt.acc);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_GPS_ALTITUDE, data->pvt.alt);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_GPS_SPEED, data->pvt.spd);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_GPS_HEADING, data->pvt.hdg);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj(parent, DATA_VALUE, obj);

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return 0;
}

static int json_encode_gps_value_nmea(cJSON *parent, struct cloud_data_gps *data)
{
	int err;

	err = json_add_str(parent, DATA_VALUE, data->nmea);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		return err;
	}

	return 0;
}

static int json_encode_gps_data(cJSON *parent, struct cloud_data_gps *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->gps_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->gps_ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	switch (data->format) {
	case CLOUD_CODEC_GPS_FORMAT_PVT:
		json_encode_gps_value_pvt(obj, data);
		break;
	case CLOUD_CODEC_GPS_FORMAT_NMEA:
		json_encode_gps_value_nmea(obj, data);
		break;
	default:
		LOG_WRN("GPS data format not set");
		err = -EINVAL;
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_sensor_value(cJSON *parent, struct cloud_data_sensors *data)
{
	cJSON *obj;
	int err;

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, DATA_TEMPERATURE, data->temp);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_HUMID, data->hum);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj(parent, DATA_ENVIRONMENTALS, obj);

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_sensor_data(cJSON *parent, struct cloud_data_sensors *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->env_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->env_ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_encode_sensor_value(obj, data);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_accel_value(cJSON *parent, struct cloud_data_accelerometer *data)
{
	cJSON *obj;
	int err;

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, DATA_MOVEMENT_X, data->values[0]);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_MOVEMENT_Y, data->values[1]);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_MOVEMENT_Z, data->values[2]);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj(parent, DATA_MOVEMENT, obj);

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_accel_data(cJSON *parent, struct cloud_data_accelerometer *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_encode_accel_value(obj, data);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
}

static int json_encode_battery_data(cJSON *parent, struct cloud_data_battery *data)
{
	cJSON *obj;
	int err;

	if (!data->queued) {
		return -ENODATA;
	}

	err = date_time_uptime_to_unix_time_ms(&data->bat_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	obj = cJSON_CreateObject();
	if (!obj) {
		return -ENOMEM;
	}

	err = json_add_number(obj, "t", data->bat_ts);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	err = json_add_number(obj, DATA_BATTERY, data->bat);
	if (err) {
		LOG_ERR("Encoding error: %d returned at %s:%d", err, __FILE__, __LINE__);
		goto delete_obj;
	}

	json_add_obj_array(parent, obj);

	data->queued = false;

	return 0;

delete_obj:
	cJSON_Delete(obj);

	return err;
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
	cJSON *array;
	int err;
	char *buffer;
	bool object_added = false;

	array = cJSON_CreateArray();
	if (!array) {
		return -ENOMEM;
	}

	err = json_encode_ui_data(array, ui_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_modem_static_data(array, modem_stat_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_modem_dynamic_data(array, modem_dyn_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_gps_data(array, gps_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_sensor_data(array, sensor_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_accel_data(array, accel_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	err = json_encode_battery_data(array, bat_buf);
	if (err == 0) {
		object_added = true;
	} else if (err != -ENODATA) {
		goto delete_array;
	}

	if (!object_added) {
		err = -ENODATA;
		LOG_DBG("No data to encode, JSON string empty...");
		goto delete_array;
	} else {
		/* At this point err can be either 0 or -ENODATA. Explicitly set
		 * err to 0 if objects has been added to the array.
		 */
		err = 0;
	}

	buffer = cJSON_PrintUnformatted(array);
	if (!buffer) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto delete_array;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		json_print_obj("Encoded message:\n", array);
	}

	output->buf = buffer;
	output->len = strlen(buffer);

delete_array:
	cJSON_Delete(array);

	return err;
}

int cloud_codec_encode_ui_data(struct cloud_codec_data *output,
			       struct cloud_data_ui *ui_buf)
{
	cJSON *array;
	int err;
	char *buffer;

	array = cJSON_CreateArray();
	if (!array) {
		return -ENOMEM;
	}

	err = json_encode_ui_data(array, ui_buf);
	if (err) {
		goto delete_array;
	}

	buffer = cJSON_PrintUnformatted(array);
	if (!buffer) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto delete_array;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		json_print_obj("Encoded message:\n", array);
	}

	output->buf = buffer;
	output->len = strlen(buffer);

delete_array:
	cJSON_Delete(array);

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
	cJSON *array;
	int err;
	char *buffer;
	bool object_added = false;
	size_t i;

	array = cJSON_CreateArray();
	if (!array) {
		return -ENOMEM;
	}

#define FOREACH_BUF(_buf, _count, _encode)			\
	for (i = 0; i < _count; i++) {				\
		err = _encode(array, &_buf[i]);			\
		if (err == 0) {					\
			object_added = true;			\
		} else if (err != -ENODATA) {			\
			goto delete_array;			\
		}						\
	}

	FOREACH_BUF(ui_buf, ui_buf_count, json_encode_ui_data);
	FOREACH_BUF(modem_dyn_buf, modem_dyn_buf_count, json_encode_modem_dynamic_data);
	FOREACH_BUF(gps_buf, gps_buf_count, json_encode_gps_data);
	FOREACH_BUF(sensor_buf, sensor_buf_count, json_encode_sensor_data);
	FOREACH_BUF(accel_buf, accel_buf_count, json_encode_accel_data);
	FOREACH_BUF(bat_buf, bat_buf_count, json_encode_battery_data);

#undef FOREACH_BUF

	if (!object_added) {
		err = -ENODATA;
		LOG_DBG("No data to encode, JSON string empty...");
		goto delete_array;
	} else {
		/* At this point err can be either 0 or -ENODATA. Explicitly set err to 0 if
		 * objects has been added to the rootj object.
		 */
		err = 0;
	}

	buffer = cJSON_PrintUnformatted(array);
	if (!buffer) {
		LOG_ERR("Failed to allocate memory for JSON string");
		err = -ENOMEM;
		goto delete_array;
	}

	if (IS_ENABLED(CONFIG_CLOUD_CODEC_LOG_LEVEL_DBG)) {
		json_print_obj("Encoded message:\n", array);
	}

	output->buf = buffer;
	output->len = strlen(buffer);

delete_array:
	cJSON_Delete(array);

	return err;
}
