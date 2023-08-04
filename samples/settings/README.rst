Golioth Settings sample
#######################

Overview
********

This sample application demonstrates how to use the Golioth Settings service.
Additionally, it shows how to enable Zephyr Settings subsystem and
use it for storing Golioth credentials, as well as how to provision
these credentials using the device shell CLI.

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

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/settings``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/settings
   $ west flash

Configure WiFi SSID and PSK using the device shell and reboot:

.. code-block:: console

   uart:~$ settings set wifi/ssid <my-ssid>
   uart:~$ settings set wifi/psk <my-psk>
   uart:~$ kernel reboot cold

nRF9160 DK
----------

On your host computer open a terminal window, locate the directory that contains
the sample folder (i.e., ``~/zephyr-nrf/modules/lib/golioth``). We will build it
without assigning any Golioth credentials, and this sample automatically builds
for MCUboot. Build and flash examples are below:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/settings
   $ west flash

ESP32
-----

Configure the following Kconfig options based on your WiFi AP credentials
by adding these lines to configuration file (e.g. ``prj.conf`` or
``board/esp32_devkitc_wroom.conf``):

.. code-block:: cfg

   CONFIG_GOLIOTH_SAMPLE_WIFI_SSID="my-wifi"
   CONFIG_GOLIOTH_SAMPLE_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/settings``) and type:

.. code-block:: console

   $ west build -b esp32_devkitc_wroom samples/settings
   $ west flash

.. _authentication methods: https://docs.golioth.io/firmware/zephyr-device-sdk/authentication/
.. _Zephyr Settings subsystem: https://docs.zephyrproject.org/latest/services/settings/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
