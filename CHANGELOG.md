# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.1] - 2022-09-26
### Fixed
- added missing `boards/esp32.overlay` in `samples/hello/`
- removed outdated documentation about `esp32` WiFi credentials setup in `samples/dfu/`

## [0.3.0] - 2022-09-20
### Added
- more 'on_message' callbacks
- RPC feature
- Settings feature
- configurable TLS credentials tag for Golioth system_client module
- samples/test/ for runtime testing with mimxrt1060_evkb and nrf52840dk_nrf52840 platforms
- support for cert-based authentication
- 'settings list' shell command
- links to API and external Golioth docs
- mimxrt1060_evkb board overlays
- gracefully handle case when no DFU releases were rolled out yet
- initial twister runtime scripts support utilizing pytest harness and goliothctl tool

### Fixed
- fixed Doxygen build warnings
- fixed all 'LightDB' and 'LightDB Stream' spelling

### Changed
- updated lightdb_led sample to handle "no more items" QCBOR error as an expected condition
- use CONFIG_GOLIOTH_SAMPLE_WIFI_{SSID,PSK} credentials in samples/settings/, similar to other
  samples
- fixed miscellaneous typos
- removed 'net' tag from all sample.yaml files, which was the reason of filtering out 'esp32'
  platform by twister
- verified with Zephyr post v3.2.0-rc2 (e1cb0845b49b6a4100c9e5558d37667b92f0d000)
- verified with NCS post v2.0.0 / pre v2.1.0 (a897e619b5ac15bb27f47affd4d42c6cf8e1f49f)
- explicitly use 'application/json' instead of 'text/plain' content formats
- use 'enum golioth_content_format' instead of 'enum coap_content_format' in all APIs
- all samples wait for valid connection before sending first request/packet to server
- dropped use of deprecated 'label' DT property
- bring back "LED <num> -> <value>" log messages in samples/lightdb_led/
- replaced wifi_connect() with more generic net_connect() in samples/common/
- enable Github workflows on every PR (which was limited to 'main' branch before)
- settings shell message cleanups
- reduced configured k_malloc() heap size for 'esp32' platform in all samples

### Removed
- dropped support for plaintext/unsecure UDP transport
- dropped support for Zephyr logging v1

## [0.2.0] - 2022-07-01
### Added
- enabled `kernel reboot` command in `samples/settings/` sample
- validating of credentials at runtime:
  - PSK-ID: must contain `@` character
  - PSK: cannot be empty
- enabled use of settings subsystem for storing credentials in DFU (`samples/dfu/`) sample
- consistent use of `zsock_` prefix for socket APIs to keep compatibility with `CONFIG_POSIX_API=y`
- added Kconfig option for Golioth system thread priority
- enforced DTLS handshake as part of `golioth_connect()` API
- added `golioth_is_connected()` API, which returns status of client connection
- verified with Zephyr v3.1.0 (and updated `west-zephyr.yml`)
- verified NCS v2.0.0 (and updated `west-ncs.yml`)

### Fixed
- fixed error code propagation from `golioth_send_coap()`
- fixed Golioth logging backend registration (which fixes log filtering)
- skip deploying docs to dev if in fork PR

### Breaking Changes
- moved `net/wifi/` to `samples/common/`

### Changed
- updated  Zephyr CI docker images in GitHub Actions and GitLab CI to v0.23.3
- adjusted gitlint line length with checkpatch and editorconfig
- use of west-ncs.yml in GitLab CI, instead of direct manipulation on `.west/config`
- moved `settings` shell command implementation to `samples/common/`
- updated URL of deployed doxygen site to https://zephyr-sdk-docs.golioth.io/

### Removed
- mcumgr config command group implementation
- removed unused repositories from west-zephyr.yml (`hal_st`, `hal_stm32`, `mcumgr` and `tinycbor`)

## [0.1.0] - 2022-05-06
### Added
- Initial release, verified with Zephyr version 3.0.0 and NCS version 1.7.1.
