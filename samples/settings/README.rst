Golioth Settings sample
#######################

Overview
********

This sample application demonstrates how to enable Zephyr settings subsystem and
use it for storing Golioth credentials. Additionally it shows how to provision
these credentials using devive shell CLI.

Requirements
************

- Golioth credentials
- Network connectivity

Building and Running
********************

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

Configure PSK-ID and PSK using the device shell based on your Golioth credentials:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>

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
for MCUboot. Build and flash examples are below (remember to put the device in
bootloader mode):

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/settings
   $ west flash

Configure PSK-ID and PSK using the device shell based on your Golioth credentials and reboot:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold

.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
