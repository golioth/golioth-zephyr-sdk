Golioth Settings sample
#######################

Overview
********

This sample application demonstrates how to enable Zephyr settings subsystem and
use it for storing Golioth credentials. Additionally it shows how to provision
these credentials using ``mcumgr`` CLI.

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

Configure PSK and PSK-ID using ``mcumgr`` based on your Golioth credentials:

.. code-block:: console

   mcumgr --conntype ble --connstring peer_name=Zephyr config golioth/psk-id <my-id>
   mcumgr --conntype ble --connstring peer_name=Zephyr config golioth/psk <my-pass>

Configure WiFi SSID and PSK using ``mcumgr``:

.. code-block:: console

   mcumgr --conntype ble --connstring peer_name=Zephyr config wifi/ssid <my-ssid>
   mcumgr --conntype ble --connstring peer_name=Zephyr config wifi/psk <my-pass>

.. code-block:: console

   mcumgr --conntype ble --connstring peer_name=Zephyr reset

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

To write the settings to flash, we need to send the desired values, and then
reset the application to start using them. Here is the process for the nRF9160 DK
(change the ``connstring`` as necessary for your hardware):

.. code-block:: console

   $ mcumgr --conntype=serial --connstring='dev=/dev/ttyACM0,baud=115200' config golioth/psk-id device-id@project-id
   $ mcumgr --conntype=serial --connstring='dev=/dev/ttyACM0,baud=115200' config golioth/psk device-pre-shared-key
   $ mcumgr --conntype=serial --connstring='dev=/dev/ttyACM0,baud=115200' reset

Replace the ``device-id@project-id`` and ``device-pre-shared-key`` with your
actual values. These can be found on the devices page of the Golioth Console
(https://console.golioth.io/).

.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
