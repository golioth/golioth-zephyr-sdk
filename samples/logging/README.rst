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
``board/esp32_devkitc_wroom.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_WIFI_SSID="my-wifi"
   CONFIG_GOLIOTH_SAMPLE_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b esp32_devkitc_wroom samples/logging
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
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/logging
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/logging``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/logging
   $ west flash

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

.. _authentication methods: https://docs.golioth.io/firmware/zephyr-device-sdk/authentication/
.. _Zephyr Settings subsystem: https://docs.zephyrproject.org/latest/services/settings/index.html
.. _Networking with QEMU: https://docs.zephyrproject.org/3.4.0/connectivity/networking/qemu_setup.html
.. _ESP32: https://docs.zephyrproject.org/3.4.0/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
