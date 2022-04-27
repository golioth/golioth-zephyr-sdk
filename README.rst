Getting Started
***************

Using with Zephyr RTOS
======================

Most platforms are already supported with mainline `Zephyr RTOS`_. This
repository can be added to any Zephyr based project as new `west`_ module.
However, for making things simple, this repository can also serve as `west`_
manifest repo.

Using Golioth SDK as manifest repository
----------------------------------------

Execute this command to download this repository together with all dependencies:

.. code-block:: console

   west init -m https://github.com/golioth/zephyr-sdk.git
   west update

Adding Golioth SDK to existing west project
-------------------------------------------

Alternatively add following entry to ``west.yml`` file in ``manifest/projects``
subtree of existing `west`_ based project (e.g. Zephyr RTOS):

.. code-block:: yaml

    # Golioth repository.
    - name: golioth
      path: modules/lib/golioth
      revision: main
      url: https://github.com/golioth/zephyr-sdk.git
      import:
        name-allowlist:
          - qcbor

and clone all repositories including that one by running:

.. code-block:: console

   west update

Follow `Zephyr Getting Started`_ for details on how to setup Zephyr based
projects.

Using with nRF Connect SDK
==========================

Platforms like `nRF9160 Feather`_ require `nRF Connect SDK`_ to make use of
their distinct features, which is cellular network connectivity.

Using Golioth SDK as manifest repository
----------------------------------------

Execute this command to download this repository together with all dependencies:

.. code-block:: console

   west init -m https://github.com/golioth/zephyr-sdk.git --mf west-ncs.yml
   west update

Follow `nRF Connect SDK Getting Started`_ for details on how to setup nRF
Connect SDK based projects.

Sample applications
*******************

- `Golioth DFU sample`_
- `Golioth Hello sample`_
- `Golioth Light DB get sample`_
- `Golioth Light DB observe sample`_
- `Golioth Light DB set sample`_
- `Golioth Light DB LED sample`_
- `Golioth Light DB stream sample`_
- `Golioth Logging sample`_
- `Golioth Settings sample`_

Device matrix
*************

+------------------+--------------+----------------------------------------+----------+
|Device            |Service       |Last tested                             |Last      |
|                  |              |SDK version                             |tested    |
|                  |              |                                        |Golioth   |
|                  |              |                                        |version   |
+==================+==============+========================================+==========+
|nRF91 Development |Authentication|HEAD                                    |v0.21.0   |
|Kit               +--------------+----------------------------------------+          |
|                  |Logs          |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |DFU           |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB       |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB Stream|HEAD                                    |          |
+------------------+--------------+----------------------------------------+----------+
|ESP32-WROOM-32D   |Authentication|HEAD                                    |v0.21.0   |
|                  +--------------+----------------------------------------+          |
|                  |Logs          |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |DFU           |Zephyr 3.0.0                            |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB       |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB Stream|HEAD                                    |          |
+------------------+--------------+----------------------------------------+----------+
|nRF52840 DK +     |Authentication|HEAD                                    |v0.21.0   |
|ESP32-WROOM-32D   +--------------+----------------------------------------+          |
|                  |Logs          |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |DFU           |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB       |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB Stream|HEAD                                    |          |
+------------------+--------------+----------------------------------------+----------+
|QEMU x86          |Authentication|HEAD                                    |v0.21.0   |
|                  +--------------+----------------------------------------+          |
|                  |Logs          |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |DFU           |Not supported                           |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB       |HEAD                                    |          |
|                  +--------------+----------------------------------------+          |
|                  |LightDB Stream|HEAD                                    |          |
+------------------+--------------+----------------------------------------+----------+

.. _Zephyr RTOS: https://www.zephyrproject.org/
.. _west: https://docs.zephyrproject.org/3.0.0/guides/west/index.html
.. _Zephyr Getting Started: https://docs.zephyrproject.org/3.0.0/getting_started/index.html
.. _nRF Connect SDK: https://www.nordicsemi.com/Software-and-tools/Software/nRF-Connect-SDK
.. _nRF Connect SDK Getting Started: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html
.. _nRF9160 Feather: https://www.jaredwolff.com/store/nrf9160-feather/
.. _Golioth DFU sample: samples/dfu/README.rst
.. _Golioth Hello sample: samples/hello/README.rst
.. _Golioth Light DB get sample: samples/lightdb/get/README.rst
.. _Golioth Light DB observe sample: samples/lightdb/observe/README.rst
.. _Golioth Light DB set sample: samples/lightdb/set/README.rst
.. _Golioth Light DB LED sample: samples/lightdb_led/README.rst
.. _Golioth Light DB stream sample: samples/lightdb_stream/README.rst
.. _Golioth Logging sample: samples/logging/README.rst
.. _Golioth Settings sample: samples/settings/README.rst
