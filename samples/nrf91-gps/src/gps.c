/*
 * Copyright (c) 2021 Golioth, Inc.
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <net/socket.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>

#include "gps.h"

#define AT_XSYSTEMMODE      "AT\%XSYSTEMMODE=1,0,1,0"
#define AT_ACTIVATE_GPS     "AT+CFUN=31"
#define AT_ACTIVATE_LTE     "AT+CFUN=21"
#define AT_DEACTIVATE_LTE   "AT+CFUN=20"

#define GNSS_INIT_AND_START 1
#define GNSS_STOP           2
#define GNSS_RESTART        3

#define AT_CMD_SIZE(x) (sizeof(x) - 1)

#ifdef CONFIG_BOARD_NRF9160DK_NRF9160NS
#define AT_MAGPIO      "AT\%XMAGPIO=1,0,0,1,1,1574,1577"
#ifdef CONFIG_GPS_SAMPLE_ANTENNA_ONBOARD
#define AT_COEX0       "AT\%XCOEX0=1,1,1565,1586"
#elif CONFIG_GPS_SAMPLE_ANTENNA_EXTERNAL
#define AT_COEX0       "AT\%XCOEX0"
#endif
#endif /* CONFIG_BOARD_NRF9160DK_NRF9160NS */

#ifdef CONFIG_BOARD_THINGY91_NRF9160NS
#define AT_MAGPIO      "AT\%XMAGPIO=1,1,1,7,1,746,803,2,698,748,2,1710,2200," \
			"3,824,894,4,880,960,5,791,849,7,1565,1586"
#ifdef CONFIG_GPS_SAMPLE_ANTENNA_ONBOARD
#define AT_COEX0       "AT\%XCOEX0=1,1,1565,1586"
#elif CONFIG_GPS_SAMPLE_ANTENNA_EXTERNAL
#define AT_COEX0       "AT\%XCOEX0"
#endif
#endif /* CONFIG_BOARD_THINGY91_NRF9160NS */

#ifdef CONFIG_BOARD_CIRCUITDOJO_FEATHER_NRF9160NS
#define AT_COEX0 		"AT%XCOEX0=1,1,1565,1586"
#endif /* CONFIG_BOARD_CIRCUITDOJO_FEATHER_NRF9160NS */

static int                   gnss_fd;
static char                  nmea_strings[10][NRF_GNSS_NMEA_MAX_LEN];
static uint32_t              nmea_string_cnt;

static bool                  got_fix = false;
static uint64_t              fix_timestamp;
static nrf_gnss_data_frame_t in_progress_pvt = { 0 };

static const char status1[] = "+CEREG: 1";
static const char status2[] = "+CEREG:1";
static const char status3[] = "+CEREG: 5";
static const char status4[] = "+CEREG:5";

K_SEM_DEFINE(lte_ready, 0, 1);

static const char *const at_commands[] = {
	AT_XSYSTEMMODE,
#if defined(CONFIG_BOARD_NRF9160DK_NRF9160NS) || \
	defined(CONFIG_BOARD_THINGY91_NRF9160NS)
	AT_MAGPIO,
	AT_COEX0,
#elif defined(CONFIG_BOARD_CIRCUITDOJO_FEATHER_NRF9160NS) 
	AT_COEX0,
#endif
	AT_ACTIVATE_GPS
};

static int setup_modem(void)
{
    for (int i = 0; i < ARRAY_SIZE(at_commands); i++) {
        int err = at_cmd_write(at_commands[i], NULL, 0, NULL);
        if (err != 0) {
            printk("Failed to write AT command[%d], err: %d\n", i, err);
            return -1;
        }
    }

    return 0;
}

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

static int gnss_ctrl(uint32_t ctrl)
{
    int retval;

    nrf_gnss_fix_retry_t    fix_retry    = 0;
    nrf_gnss_fix_interval_t fix_interval = 1;
    nrf_gnss_delete_mask_t	delete_mask  = 0;
    nrf_gnss_nmea_mask_t	nmea_mask    = NRF_GNSS_NMEA_GSV_MASK |
                           NRF_GNSS_NMEA_GSA_MASK |
                           NRF_GNSS_NMEA_GLL_MASK |
                           NRF_GNSS_NMEA_GGA_MASK |
                           NRF_GNSS_NMEA_RMC_MASK;

    if (ctrl == GNSS_INIT_AND_START) {
        gnss_fd = nrf_socket(NRF_AF_LOCAL,
                     NRF_SOCK_DGRAM,
                     NRF_PROTO_GNSS);

        if (gnss_fd >= 0) {
            printk("GPS Socket created\n");
        } else {
            printk("Could not init socket (err: %d)\n", gnss_fd);
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd,
                    NRF_SOL_GNSS,
                    NRF_SO_GNSS_FIX_RETRY,
                    &fix_retry,
                    sizeof(fix_retry));
        if (retval != 0) {
            printk("Failed to set fix retry value\n");
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd,
                    NRF_SOL_GNSS,
                    NRF_SO_GNSS_FIX_INTERVAL,
                    &fix_interval,
                    sizeof(fix_interval));
        if (retval != 0) {
            printk("Failed to set fix interval value\n");
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd,
                    NRF_SOL_GNSS,
                    NRF_SO_GNSS_NMEA_MASK,
                    &nmea_mask,
                    sizeof(nmea_mask));
        if (retval != 0) {
            printk("Failed to set nmea mask\n");
            return -1;
        }
    }

    if ((ctrl == GNSS_INIT_AND_START) ||
        (ctrl == GNSS_RESTART)) {
        retval = nrf_setsockopt(gnss_fd,
                    NRF_SOL_GNSS,
                    NRF_SO_GNSS_START,
                    &delete_mask,
                    sizeof(delete_mask));
        if (retval != 0) {
            printk("Failed to start GPS\n");
            return -1;
        }
    }

    if (ctrl == GNSS_STOP) {
        retval = nrf_setsockopt(gnss_fd,
                    NRF_SOL_GNSS,
                    NRF_SO_GNSS_STOP,
                    &delete_mask,
                    sizeof(delete_mask));
        if (retval != 0) {
            printk("Failed to stop GPS\n");
            return -1;
        }
    }

    return 0;
}

int gps_init(void)
{
    int retval;

    activate_lte(false);

    if (setup_modem() != 0) {
        printk("Failed to initialize modem\n");
        return -1;
    }

    retval = gnss_ctrl(GNSS_INIT_AND_START);

    activate_lte(true);

    return retval;
}

static int gps_process_data(nrf_gnss_data_frame_t *gps_data)
{
    int retval;

    retval = nrf_recv(gnss_fd,
              &in_progress_pvt,
              sizeof(nrf_gnss_data_frame_t),
              NRF_MSG_DONTWAIT);

    if (retval > 0) {
        switch (in_progress_pvt.data_id) {
            case NRF_GNSS_PVT_DATA_ID:
                memcpy(&in_progress_pvt,
                        gps_data,
                        sizeof(nrf_gnss_data_frame_t));

                nmea_string_cnt = 0;
                got_fix = false;

                if ((in_progress_pvt.pvt.flags &
                    NRF_GNSS_PVT_FLAG_FIX_VALID_BIT)
                    == NRF_GNSS_PVT_FLAG_FIX_VALID_BIT)
                {

                    got_fix = true;
                    fix_timestamp = k_uptime_get();
                }
                break;

            case NRF_GNSS_NMEA_DATA_ID:
                if (nmea_string_cnt < 10) {
                    memcpy(nmea_strings[nmea_string_cnt++],
                        in_progress_pvt.nmea,
                        retval);
                }
                break;

            default:
                break;
        }
    }

    return retval;
}

int gps_get_data(nrf_gnss_data_frame_t *gps_data)
{
    int err;

    while (true) {
        err = gps_process_data(gps_data);
        if (err <= 0) {
            return err;
        }
    }
}

bool gps_has_fix(void)
{
    return got_fix;
}

uint64_t gps_msec_since_last_fix(void)
{
    return k_uptime_get() - fix_timestamp;
}

void gps_satellite_stats(nrf_gnss_data_frame_t *gps_data, uint8_t *tracked, uint8_t* in_fix, uint8_t *unhealthy)
{
    *tracked = 0;
    *in_fix = 0;
    *unhealthy = 0;

    for (int i = 0; i < NRF_GNSS_MAX_SATELLITES; ++i) {
        if ((in_progress_pvt.pvt.sv[i].sv > 0) &&
            (in_progress_pvt.pvt.sv[i].sv < 33))
        {
            (*tracked)++;

            if (in_progress_pvt.pvt.sv[i].flags & NRF_GNSS_SV_FLAG_USED_IN_FIX) {
                (*in_fix)++;
            }

            if (in_progress_pvt.pvt.sv[i].flags & NRF_GNSS_SV_FLAG_UNHEALTHY) {
                (*unhealthy)++;
            }
        }
    }
}
