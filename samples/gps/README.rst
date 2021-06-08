Golioth GPS Sample
##################

Overview
********

This GPS application demonstrates how to retrieve GPS data from the onboard GPS
and periodically send structured GPS fix data to a LightDB Stream. In this application,
data is sent to the ``/gps`` LightDB Stream path.

Requirements
************

- Golioth credentials
- Network connectivity
- Circuitdojo nRF9160 Feather

Building and Running
********************

Configure the following KConfig options based on your Golioth credentials

- GOLIOTH_SYSTEM_CLIENT_PSK_ID  - PSK ID of a registered device
- GOLIOTH_SYSTEM_CLIENT_PSK     - PSK of a registered device

by adding these lines to a configuration file (e.g. ``prj.conf``):

.. code-block:: cfg

    CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK_ID="nrf9160-psk-id"
    CONFIG_GOLIOTH_SYSTEM_CLIENT_PSK="nrf9160-psk"

Platform specific configuration
===============================

nRF9160 Feather
---------------

.. note::
    The nRF9160 is currently the only board supported by this sample.

On your host computer, open a terminal window, locate the source code of
this sample application (e.g. ``samples/gps``) and type:

.. code-block:: console

    $ west build -b circuitdojo_feather_nrf9160ns samples/gps

Flash the firmware with ``mcumgr`` or ``newtmgr``:

.. code-block:: console

    $ newtmgr -c serial image upload samples/gps/build/zephyr/app_update.bin

See `nRF9160 Feather Programming and Debugging`_ for more details.

