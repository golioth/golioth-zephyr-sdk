
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

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */