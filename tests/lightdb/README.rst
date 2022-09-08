Golioth LightDB test
####################

Overview
********

This test application verifies basic LightDB operations. For now just ``qemu_x86`` platform is
supported.

Requirements
************

- Golioth credentials
- Network connectivity (see `Networking with QEMU`_)

Building and Running
********************

Test application depends on specific LightDB content. In order to provide that, run
``scripts/golioth-setup.sh`` script located in this test application directory.

Setup server information (if using self-hosted server) and credentials by setting following
environment variables:

.. code-block:: shell

   export GOLIOTH_SYSTEM_SERVER_HOST="192.0.2.2"
   export GOLIOTH_SYSTEM_CLIENT_PSK_ID="my-psk-id"
   export GOLIOTH_SYSTEM_CLIENT_PSK="my-psk"

Run ``twister``:

.. code-block:: shell

   zephyr/scripts/twister -p qemu_x86 -T modules/lib/golioth/tests/lightdb

.. _Networking with QEMU: https://docs.zephyrproject.org/3.0.0/guides/networking/qemu_setup.html#networking-with-qemu
