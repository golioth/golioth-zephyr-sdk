# samples/test

Sample app that runs automated tests on the target hardware.

The verify.py script will connect to the device over serial and verify
the pass/fail result of the test suite. It returns 0 only if all tests pass (1 otherwise).

The verify.py script requires a file named `credentials.yml` (ignored by git) in
the `samples/test` directory in order to provision Golioth credentials to the device:

```yaml
settings:
  wifi/ssid: MyWiFiSSID
  wifi/psk: MyWiFiPassword
  golioth/psk-id: device@project
  golioth/psk: supersecret
```

The sample will connect to `coap.golioth.dev` by default, so ensure that the
Golioth PSK-ID and PSK matches an existing device from `coap.golioth.dev`.

### Install pre-requisities

```sh
pip install pyyaml
```

### nRF9160DK

To build, flash, and check the test execution results on the `nrf9160dk`:

```sh
west build -b nrf9160dk_nrf9160_ns . -p
west flash
python verify.py /dev/ttyACM0
```

### nRF52840 + ESP32-AT

Hardware setup is the same as described in other samples (e.g. samples/hello).

```sh
west build -b nrf52840dk_nrf52840 . -p
west flash
python verify.py /dev/ttyACM0
```
