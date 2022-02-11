Golioth Light DB stream sample
##############################

Overview
********

This Light DB stream application demonstrates how to connect with Golioth and
periodically send data to Light DB stream. In this sample temperature
measurements are sent to ``/temp`` Light DB stream path. For platforms that do
not have temperature sensor a value is generated from 20 up to 30.

Requirements
************

- Golioth credentials
- Network connectivity

Building and Running
********************

Configure the following Kconfig options based on your Golioth credentials:

- GOLIOTH_SYSTEM_CLIENT_PSK_ID  - PSK ID of registered device
- GOLIOTH_SYSTEM_CLIENT_PSK     - PSK of registered device

by adding these lines to configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID="my-psk-id"
   CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK="my-psk"

Platform specific configuration
===============================

QEMU
----

This application has been built and tested with QEMU x86 (qemu_x86) and QEMU ARM
Cortex-M3 (qemu_cortex_m3).

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
``board/esp32.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_WIFI_SSID="my-wifi"
   CONFIG_GOLIOTH_SAMPLE_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/lightdb_stream``) and type:

.. code-block:: console

   $ west build -b esp32 samples/lightdb_stream
   $ west flash

See `ESP32`_ for details on how to use ESP32 board.

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

+-----------+--------------+
|nRF52840 DK|ESP32-WROOM-32|
|           |              |
+-----------+--------------+
|P1.01 (RX) |IO17 (TX)     |
+-----------+--------------+
|P1.02 (TX) |IO16 (RX)     |
+-----------+--------------+
|P1.03 (CTS)|IO14 (RTS)    |
+-----------+--------------+
|P1.04 (RTS)|IO15 (CTS)    |
+-----------+--------------+
|P1.05      |EN            |
+-----------+--------------+
|GND        |GND           |
+-----------+--------------+

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

nRF9160-based devices
---------------------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/ligthdb_stream``) and type:

.. code-block:: console

   $ #Build for the Circuit Dojo nrf9160 Feather:
   $ west build -b circuitdojo_feather_nrf9160_ns samples/lightdb_stream
   $ #or build for the Thingy:91:
   $ west build -b thingy91_nrf9160_ns samples/lightdb_stream

Enter bootloader and use ``mcumgr`` (or ``newtmgr``) to flash firmware:

.. code-block:: console

   $ #Flashing the Circuit Dojo nRF9160 Feather
   $ mcumgr --conntype=serial --connstring='dev=/dev/ttyUSB0,baud=1000000' image upload build/zephyr/app_update.bin
   $ #Flashing example for Thingy:91
   $ mcumgr --conntype=serial --connstring='dev=/dev/ttyACM0,baud=115200' image upload build/zephyr/app_update.bin

See `nRF9160 Feather Programming and Debugging`_ for details.

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:00.000,000] <inf> golioth_system: Initializing
   [00:00:00.000,000] <inf> net_config: Initializing network
   [00:00:00.000,000] <inf> net_config: IPv4 address: 192.0.2.1
   [00:00:00.000,000] <dbg> golioth_lightdb_stream.main: Start Light DB stream sample
   [00:00:00.000,000] <dbg> golioth_lightdb_stream.main: Sending temperature 20.000000
   [00:00:00.000,000] <inf> golioth_system: Starting connect
   [00:00:00.010,000] <inf> golioth_system: Client connected!
   [00:00:05.010,000] <dbg> golioth_lightdb_stream.main: Sending temperature 20.500000
   [00:00:10.040,000] <dbg> golioth_lightdb_stream.main: Sending temperature 21.000000
   [00:00:15.050,000] <dbg> golioth_lightdb_stream.main: Sending temperature 21.500000
   [00:00:20.060,000] <dbg> golioth_lightdb_stream.main: Sending temperature 22.000000
   [00:00:25.070,000] <dbg> golioth_lightdb_stream.main: Sending temperature 22.500000
   [00:00:30.080,000] <dbg> golioth_lightdb_stream.main: Sending temperature 23.000000
   [00:00:35.090,000] <dbg> golioth_lightdb_stream.main: Sending temperature 23.500000
   [00:00:40.100,000] <dbg> golioth_lightdb_stream.main: Sending temperature 24.000000
   [00:00:45.110,000] <dbg> golioth_lightdb_stream.main: Sending temperature 24.500000
   [00:00:50.120,000] <dbg> golioth_lightdb_stream.main: Sending temperature 25.000000
   [00:00:55.130,000] <dbg> golioth_lightdb_stream.main: Sending temperature 25.500000
   [00:01:00.140,000] <dbg> golioth_lightdb_stream.main: Sending temperature 26.000000

Monitor temperature value over time
===================================

Device sends temperature measurements every 5s and updates ``/temp`` resource in
Light DB stream. Current value can be fetched using following command:

.. code-block:: console

   $ goliothctl stream get <device-id> /temp
   26

Historical data can be queried using following command:

.. code-block:: console

   $ goliothctl stream query --interval 5m --field time --field temp | jq ''
   [
     {
       "temp": 26,
       "time": "2021-06-08 12:17:01.158 +0000 UTC"
     },
     {
       "temp": 25.5,
       "time": "2021-06-08 12:16:56.146 +0000 UTC"
     },
     {
       "temp": 25,
       "time": "2021-06-08 12:16:51.138 +0000 UTC"
     },
     {
       "temp": 24.5,
       "time": "2021-06-08 12:16:46.126 +0000 UTC"
     },
     {
       "temp": 24,
       "time": "2021-06-08 12:16:41.114 +0000 UTC"
     },
     {
       "temp": 23.5,
       "time": "2021-06-08 12:16:36.108 +0000 UTC"
     },
     {
       "temp": 23,
       "time": "2021-06-08 12:16:31.098 +0000 UTC"
     },
     {
       "temp": 22.5,
       "time": "2021-06-08 12:16:26.088 +0000 UTC"
     },
     {
       "temp": 22,
       "time": "2021-06-08 12:16:21.078 +0000 UTC"
     },
     {
       "temp": 21.5,
       "time": "2021-06-08 12:16:16.068 +0000 UTC"
     },
     {
       "temp": 21,
       "time": "2021-06-08 12:16:11.054 +0000 UTC"
     },
     {
       "temp": 20.5,
       "time": "2021-06-08 12:16:06.049 +0000 UTC"
     }
   ]


.. _Networking with QEMU: https://docs.zephyrproject.org/latest/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/latest/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
.. _nRF9160 Feather Programming and Debugging: https://docs.jaredwolff.com/nrf9160-programming-and-debugging.html
