/*
 * Copyright (c) 2021 Golioth, Inc.
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef GPS_H
#define GPS_H

#include <nrf_socket.h>

#ifdef __cplusplus
extern "C" {
#endif

int gps_init(void);

int gps_process_data(nrf_gnss_data_frame_t *gps_data);

bool gps_has_fix(void);
uint64_t gps_msec_since_last_fix(void);
void gps_satellite_stats(nrf_gnss_data_frame_t *gps_data, uint8_t *tracked, uint8_t* in_fix, uint8_t *unhealthy);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */