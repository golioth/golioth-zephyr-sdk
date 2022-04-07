Golioth Logging sample
######################

Overview
********

This sample application demonstrates how to connect with Golioth and configure
logging backend to send system logs to Golioth.

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
of this sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/logging
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
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b esp32 samples/logging
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
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/logging
   $ west flash

nRF9160-based devices
---------------------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ #Build for the Circuit Dojo nrf9160 Feather:
   $ west build -b circuitdojo_feather_nrf9160_ns samples/logging
   $ #or build for the Thingy:91:
   $ west build -b thingy91_nrf9160_ns samples/logging

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

   [00:00:00.100,000] <wrn> net_sock_tls: No entropy device on the system, TLS communication may be insecure!
   [00:00:00.100,000] <inf> net_config: Initializing network
   [00:00:00.100,000] <inf> net_config: IPv4 address: 192.0.2.1
   [00:00:00.100,000] <dbg> golioth_logging.main: Start Logging sample
   [00:00:00.100,000] <inf> golioth_logging: Initializing golioth client
   [00:00:00.100,000] <inf> golioth_logging: Golioth client initialized
   [00:00:00.100,000] <dbg> golioth_logging.main: Debug info! 0
   [00:00:00.100,000] <dbg> golioth_logging.func_1: Log 1: 0
   [00:00:00.100,000] <dbg> golioth_logging.func_2: Log 2: 0
   [00:00:00.100,000] <wrn> golioth_logging: Warn: 0
   [00:00:00.100,000] <err> golioth_logging: Err: 0
   [00:00:00.100,000] <inf> golioth_logging: Counter hexdump
                                             00 00 00 00                                      |....
   [00:00:00.100,000] <inf> golioth_logging: Starting connect
   [00:00:00.110,000] <inf> golioth_logging: Client connected!
   [00:00:05.110,000] <dbg> golioth_logging.main: Debug info! 1
   [00:00:05.110,000] <dbg> golioth_logging.func_1: Log 1: 1
   [00:00:05.110,000] <dbg> golioth_logging.func_2: Log 2: 1
   [00:00:05.110,000] <wrn> golioth_logging: Warn: 1
   [00:00:05.110,000] <err> golioth_logging: Err: 1
   [00:00:05.110,000] <inf> golioth_logging: Counter hexdump
                                             01 00 00 00                                      |....
   [00:00:10.120,000] <dbg> golioth_logging.main: Debug info! 2
   [00:00:10.120,000] <dbg> golioth_logging.func_1: Log 1: 2
   [00:00:10.120,000] <dbg> golioth_logging.func_2: Log 2: 2
   [00:00:10.120,000] <wrn> golioth_logging: Warn: 2
   [00:00:10.120,000] <err> golioth_logging: Err: 2
   [00:00:10.120,000] <inf> golioth_logging: Counter hexdump
                                             02 00 00 00

Access logs with goliothctl
===========================

This is how logs are visible

.. code-block:: console

   $ goliothctl logs
   [2021-04-08 14:20:32 +0000 UTC] level:WARN module:"golioth_logging" message:"Warn: 0" metadata:{fields:{key:"index" value:{number_value:9}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"golioth_logging" message:"Golioth client initialized" metadata:{fields:{key:"index" value:{number_value:5}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"golioth_logging" message:"Initializing golioth client" metadata:{fields:{key:"index" value:{number_value:4}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"net_config" message:"IPv4 address: 192.0.2.1" metadata:{fields:{key:"index" value:{number_value:2}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"golioth_logging" message:"Client connected!" metadata:{fields:{key:"index" value:{number_value:13}} fields:{key:"uptime" value:{number_value:110000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"golioth_logging" message:"Starting connect" metadata:{fields:{key:"index" value:{number_value:12}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:ERROR module:"golioth_logging" message:"Err: 0" metadata:{fields:{key:"index" value:{number_value:10}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"net_config" message:"Initializing network" metadata:{fields:{key:"index" value:{number_value:1}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:WARN module:"net_sock_tls" message:"No entropy device on the system, TLS communication may be insecure!" metadata:{fields:{key:"index" value:{number_value:0}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:INFO module:"golioth_logging" message:"Counter hexdump" metadata:{fields:{key:"hexdump" value:{string_value:"AAAAAA=="}} fields:{key:"index" value:{number_value:11}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:DEBUG module:"golioth_logging" message:"Debug info! 0" metadata:{fields:{key:"func" value:{string_value:"main"}} fields:{key:"index" value:{number_value:6}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:DEBUG module:"golioth_logging" message:"Start Logging sample" metadata:{fields:{key:"func" value:{string_value:"main"}} fields:{key:"index" value:{number_value:3}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:DEBUG module:"golioth_logging" message:"Log 2: 0" metadata:{fields:{key:"func" value:{string_value:"func_2"}} fields:{key:"index" value:{number_value:8}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"
   [2021-04-08 14:20:32 +0000 UTC] level:DEBUG module:"golioth_logging" message:"Log 1: 0" metadata:{fields:{key:"func" value:{string_value:"func_1"}} fields:{key:"index" value:{number_value:7}} fields:{key:"uptime" value:{number_value:100000}}} device_id:"xxxxxxxxxxxxxxxxxxxxxxxx"

.. _Networking with QEMU: https://docs.zephyrproject.org/3.0.0/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/3.0.0/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
.. _nRF9160 Feather Programming and Debugging: https://docs.jaredwolff.com/nrf9160-programming-and-debugging.html
