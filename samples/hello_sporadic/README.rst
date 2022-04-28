Golioth Hello Sporadic sample
####################

Overview
********

This sample application demonstrates how to publish simple Hello messages
sporadically by briefly connecting each time a message is sent.

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

This application has been built and tested with QEMU x86 (qemu_x86).

On your Linux host computer, open a terminal window, locate the source code
of this sample application (i.e., ``samples/hello_sporadic``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/hello_sporadic
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
sample application (i.e., ``samples/hello_sporadic``) and type:

.. code-block:: console

   $ west build -b esp32 samples/hello_sporadic
   $ west flash

See `ESP32`_ for details on how to use ESP32 board.

nRF52840 DK + ESP32-WROOM-32
----------------------------

This subsection documents using nRF52840 DK running Zephyr with offloaded ESP-AT
WiFi driver and ESP32-WROOM-32 module based board (such as ESP32 DevkitC rev.
1) running WiFi stack. See `AT Binary Lists`_ for links to ESP-AT binaries and
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
sample application (i.e., ``samples/hello_sporadic``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/hello_sporadic
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/hello_sporadic``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/hello_sporadic
   $ west flash

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:00.208,740] <inf> golioth_system: Initializing
   [00:00:11.111,000] <dbg> golioth_hello.main: Start Hello Sporadic sample
   [00:00:11.111,000] <inf> golioth_system: Starting connect
   [00:00:11.112,000] <inf> golioth_hello: Sending hello! 0
   [00:00:11.115,000] <inf> golioth_system: Client connected!
   [00:00:11.803,000] <inf> golioth_system: Disconnect request
   [00:01:11.803,000] <inf> golioth_system: Starting connect
   [00:01:11.803,000] <inf> golioth_hello: Sending hello! 1
   [00:01:11.808,000] <inf> golioth_system: Client connected!
   [00:01:12.629,000] <inf> golioth_system: Disconnect request
   [00:02:12.628,000] <inf> golioth_system: Starting connect
   [00:02:12.629,000] <inf> golioth_hello: Sending hello! 2
   [00:02:12.633,000] <inf> golioth_system: Client connected!
   [00:02:13.350,000] <inf> golioth_system: Disconnect request
   [00:03:13.349,000] <inf> golioth_system: Starting connect
   [00:03:13.350,000] <inf> golioth_hello: Sending hello! 3
   [00:03:13.354,000] <inf> golioth_system: Client connected!
   [00:03:14.177,000] <inf> golioth_system: Disconnect request
   [00:04:14.176,000] <inf> golioth_system: Starting connect
   [00:04:14.177,000] <inf> golioth_hello: Sending hello! 4
   [00:04:14.181,000] <inf> golioth_system: Client connected!
   [00:04:14.898,000] <inf> golioth_system: Disconnect request

.. _Networking with QEMU: https://docs.zephyrproject.org/3.0.0/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/3.0.0/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
