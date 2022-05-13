Golioth DFU sample
##################

Overview
********

This DFU application demonstrates how to connect with Golioth and use Device
Firmware Upgrade (DFU) procedure.

The app is first built and flashed to the device which will then listen to
Golioth Cloud for a new firmware binary to become available. A second binary is
built with a new version number, registered as an artifact, and deployed as a
release. The device will detect this change and automatically download and
install.

Requirements
************

- Golioth credentials
- Network connectivity

nRF9160 DK with nRF Connect SDK
*******************************

Build Zephyr sample application for nRF9160 DK:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/dfu
   $ west flash

Configure PSK-ID and PSK using the device shell based on your Golioth credentials and reboot:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold

Now rebuild the application with the new version number 1.2.3 to distinguish it
from the old firmware:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/dfu -- -DCONFIG_MCUBOOT_IMAGE_VERSION=\"1.2.3\"

Start DFU using goliothctl
==========================

Run following command on host PC to upload new firmware as artifact to Golioth:

.. code-block:: console

   $ goliothctl dfu artifact create build/zephyr/app_update.bin --version 1.2.3

Then create new release consisting of this single firmware and roll it out to
all devices in a project:

.. code-block:: console

   $ goliothctl dfu release create --release-tags 1.2.3 --components main@1.2.3 --rollout true


Jump down to the `Observe DFU process in target console output`_ section lower
on this page to see the expected behavior of the target device during DFU.

Using with Zephyr
*****************

Building and flashing MCUboot
=============================

Zephyr MCUboot target
---------------------

The below steps describe how to build and run the MCUboot bootloader. Detailed
instructions can be found in the `MCUboot`_ documentation page.

The Zephyr port of MCUboot is essentially a normal Zephyr application, which
means that we can build and flash it like normal using ``west``, like so:

.. code-block:: console

   west build -b <board> -d build_mcuboot bootloader/mcuboot/boot/zephyr
   west flash -d build_mcuboot

Substitute <board> for one of the boards supported by the sample.

Espressif MCUboot target
------------------------

Follow https://github.com/mcu-tools/mcuboot/blob/399720d1cabd26c4356445d351f263b31e942961/docs/readme-espressif.md
on how to build and flash MCUboot.

Building the sample application
===============================

Platform specific configuration
-------------------------------

nRF52840 DK + ESP32-WROOM-32
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
sample application (i.e., ``samples/dfu``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/dfu

ESP32
~~~~~

Configure the following Kconfig options based on your WiFi AP credentials
by adding these lines to configuration file (e.g. ``prj.conf`` or
``board/esp32.conf``):

.. code-block:: cfg

   CONFIG_ESP32_WIFI_SSID="my-wifi"
   CONFIG_ESP32_WIFI_PSK="my-psk"

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/dfu``) and type:

.. code-block:: console

   $ west build -b esp32 samples/dfu

Signing the sample image
========================

A key feature of MCUboot is that images must be signed before they can be
successfully uploaded and run on a target. To sign images, the MCUboot tool
``imgtool`` can be used.

To sign the sample image we built in a previous step:

.. code-block:: console

   $ west sign -t imgtool -- --key WEST_ROOT/bootloader/mcuboot/root-rsa-2048.pem

The above command creates image files called ``zephyr.signed.bin`` and
``zephyr.signed.hex`` in the build directory.

For more information on image signing and ``west sign``, see the `Signing
Binaries`_ documentation.

Flashing the sample image
=========================

Upload the ``zephyr.signed.bin`` (or ``zephyr.signed.hex``) file from the
previous step to first application image slot of your board (see `Flash map`_
for details on flash partitioning). Specify *signed* image file using
``--bin-file`` option, otherwise non-signed version will be used and image won't
be runnable:

.. code-block:: console

   $ west flash --bin-file build/zephyr/zephyr.signed.bin --hex-file build/zephyr/zephyr.signed.hex

.. note::

   Some west flash runners use ``bin`` file by default, while others use ``hex``
   file. This is why paths to both ``zephyr.signed.bin`` and
   ``zephyr.signed.hex`` are specified in executed command.

Run following command in Zephyr shell to confirm content of first application
slot (primary area):

.. code-block:: console

   uart:~$ mcuboot
   swap type: none
   confirmed: 1

   primary area (1):
     version: 0.0.0+0
     image size: 221104
     image hash: f48973eed40a9d30795df7121183e7a828e9b89aa5ee84f2db1318f7cf51be0b
     magic: good
     swap type: test
     copy done: set
     image ok: set

   failed to read secondary area (2) header: -5

Configure credentials
=====================

(``nRF52840 DK + ESP32-WROOM-32`` only) Configure WiFi SSID and PSK using the device shell:

.. code-block:: console

   uart:~$ settings set wifi/ssid <my-ssid>
   uart:~$ settings set wifi/psk <my-psk>

Configure PSK-ID and PSK using the device shell based on your Golioth credentials and reboot:

.. code-block:: console

   uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
   uart:~$ settings set golioth/psk <my-psk>
   uart:~$ kernel reboot cold

Prepare new firmware
====================

For testing purposes of DFU mechanism the same firmware will be used. To
distinguish between old firmware and new firmware, a firmware version will be
assigned during image signing process. Execute following command to generate new
signed application image:

.. code-block:: console

   $ west sign -t imgtool --no-hex -B new.bin -- --key WEST_ROOT/bootloader/mcuboot/root-rsa-2048.pem --version 1.2.3

Please note the differences between this step and `Signing the sample image`_.
``bin`` version of firmware image will be used for DFU, which is why
``--no-hex`` was specified to prevent generation of ``hex`` file. ``-B new.bin``
was specified to override default path of ``bin`` file and prevent overriding
original application image from `Signing the sample image`_. ``--version 1.2.3``
was specified to distinguish between old firmware (default version is ``0.0.0``
if not explicitly specified) and new firmware.

Start DFU using goliothctl
==========================

Run following command on host PC to upload new firmware as artifact to Golioth:

.. code-block:: console

   $ goliothctl dfu artifact create new.bin --version 1.2.3

Then create new release consisting of this single firmware and roll it out to
all devices in a project:

.. code-block:: console

   $ goliothctl dfu release create --release-tags 1.2.3 --components main@1.2.3 --rollout true

Observe DFU process in target console output
============================================

DFU process should be started in Zephyr and this is what should be visible on
serial console:

.. code-block:: console

   [00:00:06.483,764] <dbg> golioth_dfu: Desired
                                         a3 01 1a 61 7a be 80 02  78 40 61 66 62 66 38 34 |...az... x@afbf84
                                         33 31 33 61 36 66 65 30  66 37 63 30 35 35 39 37 |313a6fe0 f7c05597
                                         62 36 31 37 32 38 32 30  64 31 37 65 30 64 30 39 |b6172820 d17e0d09
                                         37 63 31 32 34 35 36 31  64 34 30 34 65 38 32 34 |7c124561 d404e824
                                         37 39 32 30 64 38 66 30  39 33 03 81 a6 01 64 6d |7920d8f0 93....dm
                                         61 69 6e 02 65 31 2e 32  2e 33 03 78 40 35 30 34 |ain.e1.2 .3.x@504
                                         39 36 32 37 30 38 31 39  33 32 39 37 66 36 38 66 |96270819 3297f68f
                                         62 61 34 61 33 31 39 64  65 65 66 61 34 39 61 37 |ba4a319d eefa49a7
                                         35 31 33 32 39 30 31 31  35 36 63 32 37 31 63 62 |51329011 56c271cb
                                         31 34 65 37 39 66 63 61  38 30 33 64 66 04 1a 00 |14e79fca 803df...
                                         09 b0 a0 05 70 2f 2e 75  2f 63 2f 6d 61 69 6e 40 |....p/.u /c/main@
                                         31 2e 32 2e 33 06 67 6d  63 75 62 6f 6f 74       |1.2.3.gm cuboot
   [00:00:06.484,130] <inf> golioth: Manifest sequence-number: 1635434112
   [00:00:06.637,725] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 0
   [00:00:06.637,847] <inf> mcuboot_util: Swap type: none
   [00:00:06.637,847] <inf> golioth_dfu: swap type: none
   [00:00:06.863,555] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 1024
   [00:00:07.000,457] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 2048
   [00:00:07.137,786] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 3072
   ...
   [00:03:44.913,208] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 218112
   [00:03:44.956,146] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 219136
   [00:03:44.995,086] <dbg> golioth_dfu.data_received: Received 1024 bytes at offset 220160
   [00:03:45.030,334] <dbg> golioth_dfu.data_received: Received 768 bytes at offset 221184 (last)
   [00:03:45.210,205] <inf> golioth_dfu: Requesting upgrade
   [00:03:45.210,540] <inf> golioth_dfu: Rebooting in 1 second(s)

At this point mcuboot swaps first application slot (containing old firmware)
with second application slot (containing new firmware). After few seconds (or a
minute depending on firmware size) new firmware will be booted from first
application slot and following messages should appear on serial console:

.. code-block:: console

   *** Booting Zephyr OS build zephyr-v2.5.0-2205-g3276779c5a88  ***
   [00:00:00.008,850] <dbg> golioth_dfu.main: Start DFU sample
   [00:00:00.009,155] <inf> golioth_dfu: Initializing golioth client
   [00:00:00.009,246] <inf> golioth_dfu: Golioth client initialized
   [00:00:00.009,307] <inf> golioth_dfu: Starting connect

Execute ``mcuboot`` shell command in Zephyr to confirm that new firmware is
running from primary area (first application slot):

.. code-block:: console

   uart:~$ mcuboot
   swap type: none
   confirmed: 1

   primary area (1):
     version: 1.2.3+0
     image size: 221104
     image hash: 40710f0bd8171d7614b13da4821da57066f4431e4f3ebb473de9e95f6467ae65
     magic: good
     swap type: test
     copy done: set
     image ok: set

   secondary area (2):
     version: 0.0.0+0
     image size: 221104
     image hash: f48973eed40a9d30795df7121183e7a828e9b89aa5ee84f2db1318f7cf51be0b
     magic: unset
     swap type: none
     copy done: unset
     image ok: unset

.. _MCUboot: https://docs.zephyrproject.org/3.0.0/guides/device_mgmt/dfu.html#mcuboot
.. _Signing Binaries: https://docs.zephyrproject.org/3.0.0/guides/west/sign.html#west-sign
.. _Flash map: https://docs.zephyrproject.org/3.0.0/reference/storage/flash_map/flash_map.html#flash-map-api
.. _AT Binary Lists: https://docs.espressif.com/projects/esp-at/en/latest/AT_Binary_Lists/index.html
