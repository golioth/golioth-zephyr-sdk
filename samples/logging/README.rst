Golioth Logging sample
######################

Overview
********

This sample application demonstrates how to connect with Golioth and configure
logging backend to send system logs to Golioth.

Requirements
************

- Golioth credentials and server information
- Network connectivity

Building and Running
********************

Configure the following Kconfig options based on your Golioth credentials and
server in your own overlay config file:

- GOLIOTH_SERVER_IP_ADDR      - Server IPv4 address.
- GOLIOTH_SERVER_PORT         - Server port number.
- GOLIOTH_SERVER_DTLS_PSK_ID  - PSK ID of registered device
- GOLIOTH_SERVER_DTLS_PSK     - PSK of registered device

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

- ESP32_WIFI_SSID     - WiFi SSID
- ESP32_WIFI_PASSWORD - WiFi PSK

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b esp32 samples/logging
   $ west flash

This is the overlay template for WiFi credentials:

.. code-block:: console

   CONFIG_ESP32_WIFI_SSID="my-wifi"
   CONFIG_ESP32_WIFI_PASSWORD="my-psk"

See `ESP32`_ for details on how to use ESP32 board.

Sample overlay file
===================

This is the overlay template for Golioth credentials and server:

.. code-block:: console

   CONFIG_GOLIOTH_SERVER_DTLS_PSK_ID="my-psk-id"
   CONFIG_GOLIOTH_SERVER_DTLS_PSK="my-psk"
   CONFIG_GOLIOTH_SERVER_IP_ADDR="192.168.1.10"
   CONFIG_GOLIOTH_SERVER_PORT=5684

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:00.000,000] <inf> net_config: Initializing network
   [00:00:00.000,000] <inf> net_config: IPv4 address: 192.0.2.1
   [00:00:00.000,000] <dbg> golioth_logging.main: Start Logging sample
   [00:00:00.000,000] <inf> golioth_logging: Initializing golioth client
   [00:00:00.000,000] <inf> golioth_logging: Golioth client initialized
   [00:00:00.000,000] <dbg> golioth_logging.main: Debug info! 0
   [00:00:00.000,000] <dbg> golioth_logging.func_1: Log 1: 0
   [00:00:00.000,000] <dbg> golioth_logging.func_2: Log 2: 0
   [00:00:00.000,000] <wrn> golioth_logging: Warn: 0
   [00:00:00.000,000] <err> golioth_logging: Err: 0
   [00:00:00.000,000] <inf> golioth_logging: Counter hexdump
                                             00 00 00 00                                      |....
   [00:00:00.000,000] <inf> golioth_logging: Starting connect
   [00:00:00.000,000] <inf> golioth_logging: Client connected!
   [00:00:05.010,000] <dbg> golioth_logging.main: Debug info! 1
   [00:00:05.010,000] <dbg> golioth_logging.func_1: Log 1: 1
   [00:00:05.010,000] <dbg> golioth_logging.func_2: Log 2: 1
   [00:00:05.010,000] <wrn> golioth_logging: Warn: 1
   [00:00:05.010,000] <err> golioth_logging: Err: 1
   [00:00:05.010,000] <inf> golioth_logging: Counter hexdump
                                             01 00 00 00                                      |....
   [00:00:10.020,000] <dbg> golioth_logging.main: Debug info! 2
   [00:00:10.020,000] <dbg> golioth_logging.func_1: Log 1: 2
   [00:00:10.020,000] <dbg> golioth_logging.func_2: Log 2: 2
   [00:00:10.020,000] <wrn> golioth_logging: Warn: 2
   [00:00:10.020,000] <err> golioth_logging: Err: 2
   [00:00:10.020,000] <inf> golioth_logging: Counter hexdump
                                             02 00 00 00                                      |....

.. _Networking with QEMU: https://docs.zephyrproject.org/latest/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/latest/boards/xtensa/esp32/doc/index.html
