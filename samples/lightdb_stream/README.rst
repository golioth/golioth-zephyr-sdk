Golioth LightDB Stream sample
##############################

Overview
********

This LightDB Stream application demonstrates how to connect with Golioth and
periodically send data to LightDB Stream. In this sample temperature
measurements are sent to ``/temp`` LightDB Stream path. For platforms that do
not have temperature sensor a value is generated from 20 up to 30.

Requirements
************

- Golioth credentials
- Network connectivity

Building and Running
********************

Authentication specific configuration
=====================================

Golioth offers two `authentication methods`_: Pre-Shared Keys (PSK) or Public
Key Cryptography using Certificates (certs). Normally, it is the responsibility
of the Golioth SDK user to load these credentials at runtime. For simplicity,
we provide facilities to hardcode these credentials or set them at
runtime for our samples.

PSK based auth - Hardcoded
--------------------------

Configure the following Kconfig options based on your Golioth credentials:

- ``GOLIOTH_SAMPLE_HARDCODED_PSK_ID``  - PSK ID of registered device
- ``GOLIOTH_SAMPLE_HARDCODED_PSK``     - PSK of registered device

by adding these lines to configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK_ID="my-psk-id"
   CONFIG_GOLIOTH_SAMPLE_HARDCODED_PSK="my-psk"

PSK based auth - Runtime
------------------------

We provide an option for setting Golioth credentials through the Zephyr
shell. This is based on the `Zephyr Settings subsystem`_.

Enable the settings shell by including the following configuration overlay
file:

.. code-block:: console

   $ west build -- -DEXTRA_CONF_FILE=${ZEPHYR_GOLIOTH_MODULE_DIR}/samples/common/runtime_psk.conf

Alternatively, you can add the following options to ``prj.conf``:

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_HARDCODED_CREDENTIALS=n

   CONFIG_FLASH=y
   CONFIG_FLASH_MAP=y
   CONFIG_NVS=y

   CONFIG_SETTINGS=y
   CONFIG_SETTINGS_RUNTIME=y
   CONFIG_GOLIOTH_SAMPLE_PSK_SETTINGS=y
   CONFIG_GOLIOTH_SAMPLE_SETTINGS_AUTOLOAD=y
   CONFIG_GOLIOTH_SAMPLE_SETTINGS_SHELL=y

At runtime, configure PSK-ID and PSK using the device shell based on your
Golioth credentials:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:-$ kernel reboot cold

Certificate based auth - Hardcoded
----------------------------------

Configure the following Kconfig options based on your Golioth credentials:

- ``CONFIG_GOLIOTH_AUTH_METHOD_CERT``           - use certificate-based authentication
- ``CONFIG_GOLIOTH_SAMPLE_HARDCODED_CRT_PATH``  - device certificate
- ``CONFIG_GOLIOTH_SAMPLE_HARDCODED_KEY_PATH``  - device private key

by adding these lines to configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_AUTH_METHOD_CERT=y
   CONFIG_GOLIOTH_SAMPLE_HARDCODED_CRT_PATH="keys/device.crt.der"
   CONFIG_GOLIOTH_SAMPLE_HARDCODED_KEY_PATH="keys/device.key.der"

Platform specific configuration
===============================

QEMU
----

This application has been built and tested with QEMU x86 (qemu_x86).

On your Linux host computer, open a terminal window, locate the source code
of this sample application (i.e., ``samples/lightdb_stream``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/lightdb_stream
   $ west build -t run

See `Networking with QEMU`_ on how to setup networking on host and configure
NAT/masquerading to access Internet.

ESP32
-----

Configure the following Kconfig options based on your WiFi AP credentials:

- GOLIOTH_SAMPLE_WIFI_SSID  - WiFi SSID
- GOLIOTH_SAMPLE_WIFI_PSK   - WiFi PSK

by adding these lines to configuration file (e.g. ``prj.conf`` or
``board/esp32_devkitc_wroom.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_WIFI_SSID="my-wifi"
   CONFIG_GOLIOTH_SAMPLE_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/lightdb_stream``) and type:

.. code-block:: console

   $ west build -b esp32_devkitc_wroom samples/lightdb_stream
   $ west flash

See `ESP32-DEVKITC-WROOM`_ for details on how to use ESP32 board.

nRF52840 DK + ESP32-WROOM-32
----------------------------

This subsection documents using nRF52840 DK running Zephyr with offloaded ESP-AT
WiFi driver and ESP32-WROOM-32 module based board (such as ESP32 DevkitC rev.
4) running WiFi stack. See `AT Binary Lists`_ for links to ESP-AT binaries and
details on how to flash ESP-AT image on ESP chip. Flash ESP chip with following
command:

.. code-block:: console

   esptool.py write_flash --verify 0x0 PATH_TO_ESP_AT/factory/factory_WROOM-32.bin

Connect nRF52840 DK and ESP32-DevKitC V4 (or other ESP32-WROOM-32 based board)
using wires:

.. table::
   :widths: auto
   :align: center

   ===========  ==============  ===============
   nRF52840 DK  ESP32-WROOM-32  ESP32-WROVER-32
   ===========  ==============  ===============
   P1.01 (RX)   IO17 (TX)       IO22 (TX)
   P1.02 (TX)   IO16 (RX)       IO19 (RX)
   P1.03 (CTS)  IO14 (RTS)      IO14 (RTS)
   P1.04 (RTS)  IO15 (CTS)      IO15 (CTS)
   P1.05        EN              EN
   GND          GND             GND
   ===========  ==============  ===============

Configure the following Kconfig options based on your WiFi AP credentials:

- GOLIOTH_SAMPLE_WIFI_SSID - WiFi SSID
- GOLIOTH_SAMPLE_WIFI_PSK  - WiFi PSK

by adding these lines to configuration file (e.g. ``prj.conf`` or
``board/nrf52840dk_nrf52840.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_WIFI_SSID="my-wifi"
   CONFIG_GOLIOTH_SAMPLE_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/lightdb_stream``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/lightdb_stream
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/ligthdb_stream``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/lightdb_stream
   $ west flash

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:00.030,000] <inf> golioth_system: Initializing
   [00:00:00.030,000] <inf> net_config: Initializing network
   [00:00:00.030,000] <inf> net_config: IPv4 address: 192.0.2.1
   [00:00:00.030,000] <dbg> golioth_lightdb_stream: main: Start LightDB Stream sample
   [00:00:00.040,000] <inf> golioth_system: Starting connect
   [00:00:00.060,000] <dbg> golioth_lightdb_stream: main: Sending temperature 20.000000
   [00:00:00.060,000] <inf> golioth_system: Client connected!
   [00:00:00.060,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed
   [00:00:05.070,000] <dbg> golioth_lightdb_stream: main: Sending temperature 20.500000
   [00:00:05.070,000] <dbg> golioth_lightdb_stream: temperature_push_handler: Temperature successfully pushed
   [00:00:10.080,000] <dbg> golioth_lightdb_stream: main: Sending temperature 21.000000
   [00:00:10.080,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed
   [00:00:15.090,000] <dbg> golioth_lightdb_stream: main: Sending temperature 21.500000
   [00:00:15.090,000] <dbg> golioth_lightdb_stream: temperature_push_handler: Temperature successfully pushed
   [00:00:20.100,000] <dbg> golioth_lightdb_stream: main: Sending temperature 22.000000
   [00:00:20.100,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed
   [00:00:25.110,000] <dbg> golioth_lightdb_stream: main: Sending temperature 22.500000
   [00:00:25.110,000] <dbg> golioth_lightdb_stream: temperature_push_handler: Temperature successfully pushed
   [00:00:30.120,000] <dbg> golioth_lightdb_stream: main: Sending temperature 23.000000
   [00:00:30.120,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed
   [00:00:35.130,000] <dbg> golioth_lightdb_stream: main: Sending temperature 23.500000
   [00:00:35.130,000] <dbg> golioth_lightdb_stream: temperature_push_handler: Temperature successfully pushed
   [00:00:40.140,000] <dbg> golioth_lightdb_stream: main: Sending temperature 24.000000
   [00:00:40.140,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed
   [00:00:45.150,000] <dbg> golioth_lightdb_stream: main: Sending temperature 24.500000
   [00:00:45.150,000] <dbg> golioth_lightdb_stream: temperature_push_handler: Temperature successfully pushed
   [00:00:50.160,000] <dbg> golioth_lightdb_stream: main: Sending temperature 25.000000
   [00:00:50.160,000] <dbg> golioth_lightdb_stream: temperature_push_sync: Temperature successfully pushed

Monitor temperature value over time
===================================

Device sends temperature measurements every 5s and updates ``/temp`` resource in
LightDB Stream. Current value can be fetched using following command:

.. code-block:: console

   $ goliothctl stream get <device-id> /temp
   25

Data can be be observed in realtime using following command:

.. code-block:: console

   $ goliothctl stream listen
   {"timestamp":"2022-09-09T12:46:22.294832197Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":20}}
   {"timestamp":"2022-09-09T12:46:27.301030227Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":20.5}}
   {"timestamp":"2022-09-09T12:46:32.314922477Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":21}}
   {"timestamp":"2022-09-09T12:46:37.321291988Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":21.5}}
   {"timestamp":"2022-09-09T12:46:42.334931934Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":22}}
   {"timestamp":"2022-09-09T12:46:47.344960716Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":22.5}}
   {"timestamp":"2022-09-09T12:46:52.354604450Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":23}}
   {"timestamp":"2022-09-09T12:46:57.362001530Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":23.5}}
   {"timestamp":"2022-09-09T12:47:02.374861331Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":24}}
   {"timestamp":"2022-09-09T12:47:07.384704973Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":24.5}}
   {"timestamp":"2022-09-09T12:47:12.394896354Z", "deviceId":"6033cc457016b281d671df53", "data":{"temp":25}}

Historical data can be queried using following command:

.. code-block:: console

   $ goliothctl stream query --interval 5m --field time --field temp | jq ''
   [
     {
       "temp": 20,
       "time": "2022-09-09 12:46:22.294 +0000 UTC"
     },
     {
       "temp": 20.5,
       "time": "2022-09-09 12:46:27.301 +0000 UTC"
     },
     {
       "temp": 21,
       "time": "2022-09-09 12:46:32.314 +0000 UTC"
     },
     {
       "temp": 21.5,
       "time": "2022-09-09 12:46:37.321 +0000 UTC"
     },
     {
       "temp": 22,
       "time": "2022-09-09 12:46:42.334 +0000 UTC"
     },
     {
       "temp": 22.5,
       "time": "2022-09-09 12:46:47.344 +0000 UTC"
     },
     {
       "temp": 23,
       "time": "2022-09-09 12:46:52.354 +0000 UTC"
     },
     {
       "temp": 23.5,
       "time": "2022-09-09 12:46:57.362 +0000 UTC"
     },
     {
       "temp": 24,
       "time": "2022-09-09 12:47:02.374 +0000 UTC"
     },
     {
       "temp": 24.5,
       "time": "2022-09-09 12:47:07.384 +0000 UTC"
     },
     {
       "temp": 25,
       "time": "2022-09-09 12:47:12.394 +0000 UTC"
     }
   ]

.. _authentication methods: https://docs.golioth.io/firmware/zephyr-device-sdk/authentication/
.. _Zephyr Settings subsystem: https://docs.zephyrproject.org/latest/services/settings/index.html
.. _Networking with QEMU: https://docs.zephyrproject.org/3.5.0/connectivity/networking/qemu_setup.html
.. _ESP32-DEVKITC-WROOM: https://docs.zephyrproject.org/3.5.0/boards/xtensa/esp32_devkitc_wroom/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
