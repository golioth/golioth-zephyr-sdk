Golioth Light DB sample
#######################

Overview
********

This Light DB application demonstrates how to connect with Golioth and access
Light DB.

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
of this sample application (i.e., ``samples/lightdb``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/lightdb
   $ west build -t run

or

.. code-block:: console

   $ west build -b qemu_x86 samples/lightdb -- -DOVERLAY_FILE="<overlay1.conf>;<overlay2.conf>"
   $ west build -t run

See `Networking with QEMU`_ on how to setup networking on host and configure
NAT/masquerading to access Internet.

ESP32
-----

Configure the following Kconfig options based on your WiFi AP credentials:

- ESP32_WIFI_SSID     - WiFi SSID
- ESP32_WIFI_PASSWORD - WiFi PSK

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/lightdb``) and type:

.. code-block:: console

   $ west build -b esp32 samples/lightdb
   $ west flash

or

.. code-block:: console

   $ west build -b esp32 samples/lightdb -- -DOVERLAY_FILE="<overlay1.conf>;<overlay2.conf>"
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

   [00:00:00.010,000] <wrn> net_sock_tls: No entropy device on the system, TLS communication may be insecure!
   [00:00:00.010,000] <inf> net_config: Initializing network
   [00:00:00.010,000] <inf> net_config: IPv4 address: 192.0.2.1
   [00:00:00.010,000] <dbg> golioth_lightdb.main: Start Light DB sample
   [00:00:00.020,000] <inf> golioth_lightdb: Initializing golioth client
   [00:00:00.020,000] <inf> golioth_lightdb: Golioth client initialized
   [00:00:00.020,000] <inf> golioth_lightdb: Starting connect
   [00:00:00.040,000] <inf> golioth_lightdb: Client connected!
   [00:00:00.040,000] <dbg> golioth_lightdb: Payload
                                             a1 63 6d 73 67 62 4f 4b                          |.cmsgbOK
   [00:00:00.040,000] <wrn> golioth_lightdb: Map key is not boolean
   [00:00:00.040,000] <dbg> golioth_lightdb: Payload
                                             a4 61 31 f4 61 32 f5 61  33 f5 61 30 f5          |.a1.a2.a 3.a0.
   [00:00:00.040,000] <inf> golioth_lightdb: LED 1 -> 0
   [00:00:00.040,000] <inf> golioth_lightdb: LED 2 -> 1
   [00:00:00.040,000] <inf> golioth_lightdb: LED 3 -> 1
   [00:00:00.040,000] <inf> golioth_lightdb: LED 0 -> 1

Monitor counter value
=====================

Device increments counter every 5s and updates ``/counter`` resource in Light DB
with its value. Current value can be fetched using following command:

.. code-block:: console

   goliothctl lightdb get <device-id> /counter

Control LEDs
============

Multiple LEDs can be changed simultaneously using following command:

.. code-block:: console

   goliothctl lightdb set <device-id> /led -b '{"0":true,"1":false,"2":true,"3":true}'

This request should result in following serial console output:

.. code-block:: console

   [00:00:04.050,000] <dbg> golioth_lightdb: Payload
                                             a4 61 33 f5 61 30 f5 61  31 f4 61 32 f5          |.a3.a0.a 1.a2.
   [00:00:04.050,000] <inf> golioth_lightdb: LED 3 -> 1
   [00:00:04.050,000] <inf> golioth_lightdb: LED 0 -> 1
   [00:00:04.050,000] <inf> golioth_lightdb: LED 1 -> 0
   [00:00:04.050,000] <inf> golioth_lightdb: LED 2 -> 1

Additionally board LEDs will be changed, if they are configured in device-tree
as:

- ``/aliases/led0``
- ``/aliases/led1``
- ``/aliases/led2``
- ``/aliases/led3``


.. _Networking with QEMU: https://docs.zephyrproject.org/latest/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/latest/boards/xtensa/esp32/doc/index.html
