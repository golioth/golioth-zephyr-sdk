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

   $ west build -b nrf52840dk_nrf52840 samples/hello
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

Sample output
=============

This is the output from the serial console of nRF52840 DK + ESP32-WROOM-32:

.. code-block:: console

   [00:00:00.366,455] <inf> bt: Bluetooth initialized
   [00:00:00.366,912] <inf> bt: Advertising successfully started
   [00:00:00.366,943] <inf> golioth_system: Initializing
   [00:00:00.373,016] <inf> fs_nvs: 8 Sectors of 4096 bytes
   [00:00:00.373,016] <inf> fs_nvs: alloc wra: 0, f88
   [00:00:00.373,016] <inf> fs_nvs: data wra: 0, 6c
   [00:00:00.373,107] <dbg> golioth_hello.main: Start Hello sample
   [00:00:00.373,199] <dbg> golioth_wifi.wifi_settings_set: Name: ssid
   [00:00:00.373,199] <dbg> golioth_wifi: value
                                          xx xx xx xx xx xx xx                             |xxxxxxx
   [00:00:00.373,352] <dbg> golioth_wifi.wifi_settings_set: Name: psk
   [00:00:00.373,352] <dbg> golioth_wifi: value
                                          xx xx xx xx xx xx xx xx  xx xx xx xx xx xx xx xx |xxxxxxxx xxxxxxxx
   [00:00:00.373,779] <inf> golioth_hello: Connecting to WiFi
   [00:00:00.373,809] <err> golioth_wifi: Failed to request WiFi connect: -5
   [00:00:01.676,879] <inf> wifi_esp_at: ESP Wi-Fi ready
   [00:00:05.560,089] <dbg> golioth_wifi.wifi_mgmt_event_handler: wifi event: d1560003
   [00:00:05.561,035] <inf> golioth_wifi: Successfully connected to WiFi
   [00:00:05.561,035] <inf> golioth_hello: Sending hello! 0
   [00:00:05.561,065] <inf> golioth_system: Starting connect
   [00:00:05.561,309] <wrn> golioth_hello: Failed to send hello!
   [00:00:06.547,180] <inf> golioth_system: Client connected!
   [00:00:10.561,370] <inf> golioth_hello: Sending hello! 1
   [00:00:15.565,368] <inf> golioth_hello: Sending hello! 2

.. _Networking with QEMU: https://docs.zephyrproject.org/latest/guides/networking/qemu_setup.html#networking-with-qemu
.. _ESP32: https://docs.zephyrproject.org/latest/boards/xtensa/esp32/doc/index.html
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
