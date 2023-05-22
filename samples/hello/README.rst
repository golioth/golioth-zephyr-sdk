Golioth Hello sample
####################

Overview
********

This sample application demonstrates how to connect with Golioth and publish
simple Hello messages.

Requirements
************

- Golioth credentials
- Network connectivity

Building and Running
********************

Authentication specific configuration
=====================================

PSK based auth
--------------

Configure the following Kconfig options based on your Golioth credentials:

- GOLIOTH_SYSTEM_CLIENT_PSK_ID  - PSK ID of registered device
- GOLIOTH_SYSTEM_CLIENT_PSK     - PSK of registered device

by adding these lines to configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID="my-psk-id"
   CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK="my-psk"

Certificate based auth
----------------------

Configure the following Kconfig options based on your Golioth credentials:

- CONFIG_GOLIOTH_AUTH_METHOD_CERT        - use certificate-based authentication
- CONFIG_GOLIOTH_SYSTEM_CLIENT_CRT_PATH  - device certificate
- CONFIG_GOLIOTH_SYSTEM_CLIENT_KEY_PATH  - device private key

by adding these lines to configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_AUTH_METHOD_CERT=y
   CONFIG_GOLIOTH_SYSTEM_CLIENT_CRT_PATH='"keys/device.crt.der"'
   CONFIG_GOLIOTH_SYSTEM_CLIENT_KEY_PATH='"keys/device.key.der"'

Platform specific configuration
===============================

QEMU
----

This application has been built and tested with QEMU x86 (qemu_x86).

On your Linux host computer, open a terminal window, locate the source code
of this sample application (i.e., ``samples/hello``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/hello
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
sample application (i.e., ``samples/hello``) and type:

.. code-block:: console

   $ west build -b esp32 samples/hello
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
sample application (i.e., ``samples/hello``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/hello
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/hello``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/hello
   $ west flash

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:00.000,000] <inf> golioth_hello: Initializing golioth client
   [00:00:00.000,000] <inf> golioth_hello: Golioth client initialized
   [00:00:00.000,000] <inf> golioth_hello: Sending hello! 0
   [00:00:00.000,000] <wrn> golioth_hello: Failed to send hello!
   [00:00:00.000,000] <inf> golioth_hello: Starting connect
   [00:00:00.000,000] <inf> golioth_hello: Client connected!
   [00:00:05.010,000] <inf> golioth_hello: Sending hello! 1
   [00:00:05.020,000] <dbg> golioth_hello: Payload
                                           48 65 6c 6c 6f 20 6d 61  72 6b                   |Hello ma rk
   [00:00:10.030,000] <inf> golioth_hello: Sending hello! 2
   [00:00:10.030,000] <dbg> golioth_hello: Payload
                                           48 65 6c 6c 6f 20 6d 61  72 6b                   |Hello ma rk

Responses to Hello messages are printed above as a hexdump of "Hello mark". This
means that communication with Golioth is working.

.. _Networking with QEMU: https://docs.zephyrproject.org/3.3.0/connectivity/networking/qemu_setup.html
.. _ESP32: https://docs.zephyrproject.org/3.3.0/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
