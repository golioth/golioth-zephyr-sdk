Golioth Certificate Provisioning Sample
#######################################

Overview
********

This sample application demonstrates one method for provisioning certificates onto
a device for use in DTLS authentication. Certificates are loaded into the device's
filesystem using ``mcumgr``.

Requirements
************

- Golioth credentials
- Network connectivity
- A filesystem
- ``mcumgr`` CLI tool

Building and Running
********************

Platform specific configuration
===============================

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
sample application (i.e. ``samples/certificate_provisioning``) and type:

.. code-block:: console

   $ west build -b esp32_devkitc_wroom samples/certificate_provisioning
   $ west flash

See `ESP32-DEVKITC-WROOM`_ for details on how to use ESP32 board.

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
sample application (i.e., ``samples/certificate_provisioning``) and type:

.. code-block:: console

   $ west build -b nrf52840dk_nrf52840 samples/certificate_provisioning
   $ west flash

nRF9160 DK
----------

On your host computer open a terminal window, locate the source code of this
sample application (i.e., ``samples/certificate_provisioning``) and type:

.. code-block:: console

   $ west build -b nrf9160dk_nrf9160_ns samples/certificate_provisioning
   $ west flash

QEMU
----

This application has been built and tested with QEMU x86 (qemu_x86).

On your Linux host computer, open a terminal window, locate the source code
of this sample application (i.e. ``samples/certificate_provisioning``) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/certificate_provisioning
   $ west build -t run

See `Networking with QEMU`_ on how to setup networking on host and configure
NAT/masquerading to access Internet.

Installing `mcumgr`
===================

For full instructions, see `mcumgr`_.

1. Install go from https://go.dev/doc/install
2. Install the mcumgr tool:

.. code-block:: console

    $ go install github.com/apache/mynewt-mcumgr-cli/mcumgr@latest

Creating Certificates
=====================

This sample requires that you have:

- A root or intermediate certificate uploaded to the Golioth console
- A client certificate signed by the private key associated with the
  root or intermediate certificate
- The private key associated with the client certificate

For instructions on generating and positioning these certificates, see `golioth cert auth`_.

Provisioning Certificates
=========================

This sample application uses certificates stored on the device's filesystem at
``/lfs1/credentials``. It enables the ``mcumgr`` device management subsystem to
enable file upload from a host computer to the device over a serial connection.

Certificate authentication requires two files:

1. A Client Certificate, located at ``/lfs1/credentials/client_cert.der``
2. A Private Key, located at ``/lfs1/credentials/private_key.der``

Loading Files:
--------------

First, open a serial connection to the device, and enter the following commands:

.. code-block:: console

    uart:~$ fs mkdir /lfs1/credentials
    uart:~$ log halt

This will stop logs from being printed to the console to prevent them from interfering
with the file upload.

Next, exit the serial console, and from the host computer run the following:

.. code-block:: console

    $ mcumgr --conntype serial --connstring=dev=<path/to/your/device>,baud=115200 fs upload keys/client_certificate.der /lfs1/credentials/client_cert.der
    $ mcumgr --conntype serial --connstring=dev=<path/to/your/device>,baud=115200 fs upload keys/private_key.der /lfs1/credentials/private_key.der

Be sure to replace ``<path/to/your/device>`` with the appropriate serial device
for your board, typically something like ``/dev/cu.usbmodem0009600837441``.

Finally, re-open a serial connection and reset your device:

.. code-block:: console

    uart:-$ kernel reboot cold

Sample output
=============

This is the output from the serial console:

.. code-block:: console

    [00:00:00.495,513] <inf> golioth_system: Initializing
    [00:00:02.935,546] <dbg> golioth_hello: main: Start certificate provisioning sample
    [00:00:02.935,577] <inf> littlefs: LittleFS version 2.5, disk version 2.0
    [00:00:02.935,760] <inf> littlefs: FS at flash-controller@39000:0xf8000 is 6 0x1000-byte blocks with 512 cycle
    [00:00:02.935,760] <inf> littlefs: sizes: rd 16 ; pr 16 ; ca 64 ; la 32
    [00:00:02.939,270] <inf> littlefs: /lfs1 mounted
    [00:00:02.945,495] <inf> golioth_hello: Read 352 bytes from /lfs1/credentials/client_cert.der
    [00:00:02.951,538] <inf> golioth_hello: Read 121 bytes from /lfs1/credentials/private_key.der
    [00:00:02.951,599] <inf> golioth_samples: Waiting for interface to be up
    [00:00:02.951,660] <inf> golioth_system: Starting connect
    [00:00:05.833,282] <inf> golioth_system: Client connected!
    [00:00:10.833,312] <inf> golioth_hello: Sending hello! 0
    [00:00:15.833,404] <inf> golioth_hello: Sending hello! 1
    [00:00:20.833,496] <inf> golioth_hello: Sending hello! 2

.. _Networking with QEMU: https://docs.zephyrproject.org/3.5.0/connectivity/networking/qemu_setup.html
.. _ESP32-DEVKITC-WROOM: https://docs.zephyrproject.org/3.5.0/boards/xtensa/esp32_devkitc_wroom/doc/index.html
.. _mcumgr: https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html
.. _golioth cert auth: https://docs.golioth.io/firmware/zephyr-device-sdk/authentication/certificate-auth
