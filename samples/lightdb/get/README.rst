Golioth Light DB Get Sample
###########################

Overview
********

This sample demonstrates how to connect to Golioth and get values from LigthDB.

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
of this sample application (i.e., ``samples/lightdb/get``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/lightdb/get
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
sample application (i.e., ``samples/lightdb/get``) and type:

.. code-block:: console

   $ west build -b esp32 samples/lightdb/get
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
sample application (i.e., ``samples/lightdb/get``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/lightdb/get
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/ligthdb/get``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/lightdb/get
   $ west flash

Sample output
=============

This is the output from the serial console:

.. code-block:: console

   [00:00:01.080,000] <inf> golioth_system: Initializing
   [00:00:01.080,000] <inf> net_config: Initializing network
   [00:00:01.080,000] <inf> net_config: Waiting interface 1 (0x3ffb01d8) to be up...
   [00:00:01.080,000] <inf> esp_event: WIFI_EVENT_STA_START
   [00:00:01.080,000] <inf> net_config: Interface 1 (0x3ffb01d8) coming up
   [00:00:01.080,000] <inf> net_config: Running dhcpv4 client...
   [00:00:01.998,000] <inf> esp_event: WIFI_EVENT_STA_CONNECTED
   [00:00:05.102,000] <inf> net_dhcpv4: Received: 192.168.0.180
   [00:00:05.102,000] <inf> net_config: IPv4 address: 192.168.0.180
   [00:00:05.102,000] <inf> net_config: Lease time: 7200 seconds
   [00:00:05.102,000] <inf> net_config: Subnet: 255.255.255.0
   [00:00:05.102,000] <inf> net_config: Router: 192.168.0.1
   [00:00:05.102,000] <dbg> golioth_lightdb.main: Start Light DB get sample
   [00:00:05.102,000] <inf> golioth_system: Starting connect
   [00:00:05.102,000] <inf> golioth_system: Client connected!
   [00:00:13.541,000] <dbg> golioth_lightdb.reply_callback: payload: {"counter":18}
   [00:00:43.541,000] <err> golioth_system: RX client timeout!
   [00:00:43.541,000] <inf> golioth_system: Reconnect request
   [00:00:43.545,000] <inf> golioth_system: Starting connect
   [00:00:43.545,000] <inf> golioth_system: Client connected!
   [00:00:48.869,000] <dbg> golioth_lightdb.reply_callback: payload: {"counter":18}
   [00:01:18.869,000] <err> golioth_system: RX client timeout!
   [00:01:18.869,000] <inf> golioth_system: Reconnect request
   [00:01:18.873,000] <inf> golioth_system: Starting connect
   [00:01:18.873,000] <inf> golioth_system: Client connected!
   [00:01:24.507,000] <dbg> golioth_lightdb.reply_callback: payload: {"counter":18}

Set counter value
=====================

The device retrieves the value stored at ``/counter`` in LightDB every 5 seconds.
The value can be set with:

.. code-block:: console

   goliothctl lightdb set <device-name> /counter -b "{\"counter\":34}"


.. _Networking with QEMU: https://docs.zephyrproject.org/3.0.0/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/3.0.0/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
