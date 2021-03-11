Golioth Hello sample
####################

Overview
********

This sample application demonstrates how to connect with Golioth and publish
simple Hello messages. Additionally it allows to configure logging backend to
send system logs to Golioth.

Requirements
************

- Golioth credentials and server information
- Network connectivity

Building and Running
********************

This application has been built and tested with QEMU x86 and QEMU ARM Cortex-M3.

Configure the following Kconfig options based on your Golioth credentials and
server in your own overlay config file:

- GOLIOTH_HELLO_IP_ADDR      - Server IPv4 address.
- GOLIOTH_HELLO_PORT         - Server port number.
- GOLIOTH_HELLO_DTLS_PSK_ID  - PSK ID of registered device
- GOLIOTH_HELLO_DTLS_PSK     - PSK of registered device

On your Linux host computer, open a terminal window, locate the source code
of this sample application (i.e., `samples/hello`) and type:

.. code-block:: console

   $ west build -b qemu_x86 samples/hello
   $ west build -t run

or

.. code-block:: console

   $ west build -b qemu_x86 samples/hello -- -DOVERLAY_FILE="<overlay1.conf>;<overlay2.conf>"
   $ west build -t run

See `Networking with QEMU`_ on how to setup networking on host and configure
NAT/masquerading to access Internet.

Sample overlay file
===================

This is the overlay template for Golioth credentials and server:

.. code-block:: console

   CONFIG_GOLIOTH_HELLO_DTLS_PSK_ID="my-psk-id"
   CONFIG_GOLIOTH_HELLO_DTLS_PSK="my-psk"
   CONFIG_GOLIOTH_HELLO_IP_ADDR="192.168.1.10"
   CONFIG_GOLIOTH_HELLO_PORT=5684

Sample output
=============

This is the output from the QEMU console:

.. code-block:: console

   [00:00:00.000,000] <wrn> net_sock_tls: No entropy device on the system, TLS communication may be insecure!
   [00:00:00.000,000] <dbg> golioth_hello.main: Start CoAP-client sample
   [00:00:00.000,000] <inf> golioth_hello: Initializing golioth client
   [00:00:00.000,000] <inf> golioth_hello: Golioth client initialized
   [00:00:00.000,000] <inf> golioth_hello: Sending hello! 0
   [00:00:00.000,000] <dbg> golioth_hello.main: Debug info! 1
   [00:00:00.000,000] <dbg> golioth_hello.func_1: Log 1: 1
   [00:00:00.000,000] <dbg> golioth_hello.func_2: Log 2: 1
   [00:00:00.000,000] <wrn> golioth_hello: Warn: 1
   [00:00:00.000,000] <err> golioth_hello: Err: 1
   [00:00:00.000,000] <inf> golioth_hello: Starting connect
   [00:00:00.000,000] <inf> golioth_hello: Client connected!

.. _Networking with QEMU: https://docs.zephyrproject.org/latest/guides/networking/qemu_setup.html#networking-with-qemu
