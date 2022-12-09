# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.5.0] - 2022-12-13
### Added
- Python library and CLI tools for accessing Golioth REST API (`scripts/python/golioth/`)
- twister+pytest tests for `samples/rpc/`
- support for `GOLIOTHCTL_CONFIG` environment variable in pytest scripts
- `GOLIOTH_CIPHERSUITES` Kconfig option, which allows selection of preferred ciphersuites used
  during DTLS handshake (by default only one ciphersuite is selected to reduce DTLS handshake)
- configurable settings response length with `GOLIOTH_SETTINGS_MAX_RESPONSE_LEN`
- support for certificate-based authentication with `coap.golioth.io` in `sample/hello/`

### Fixed
- handling of multiple desired firmware changes in `samples/dfu/`, which previously caused corrupted
  firmware upgrade
- infinite loop in ciphersuite negotiation during DTLS handshake, solved by narrowing down used
  ciphersuites by introducing `GOLIOTH_CIPHERSUITES` Kconfig option
- handling of CoAP observation notifications (used by observations in LightDB State, DFU desired
  firmware, RPC and Settings services) with specific (too high) CoAP observe sequence numbers, which
  resulted in ignoring incoming CoAP observe notifications
- disconnecting (and reconnecting) from Golioth server on nRF9160 based devices, which sometimes
  resulted in deadlock on socket close operation
- handling of CoAP requests when disconnected from Golioth server (sending/scheduling requests is no
  longer possible when disconnected and already scheduled/pending requests are cancelled once client
  gets disconnected)

### Changed
- pytest tests for `samples/logging/` use introduced Python library instead of `goliothctl`
- hardware tests use `coap.golioth.dev` (backend development version)

## [0.4.0] - 2022-10-19
### Added
- offloaded sockets wrapper for using interruptible `poll()` with drivers implementing offloaded
  variant of this system call (workarounds limitations of nRF91 offloaded sockets)
- runtime tests with twister for most samples
- runtime `qemu_x86` tests in GitLab CI
- LightDB DELETE sample
- shared `coap_req` module used by LightDB, LightDB Stream, FW, RPC and Settings services
- CoAP packet retransmission as part of `coap_req` module implementation
- handling of DISCONNECT_RESULT WiFi management event, mainly to handle buggy WiFi drivers
  signalling such event for failed connection attempt
- exponential backoff mechanism for subsequent reconnect attempts in samples
- `qemu_x86` platform overlay for Settings sample

### Breaking Changes
- reworked LightDB, LightDB Stream and FW APIs to be CoAP agnostic; see following commits for how to
  migrate:
  - [a5400990dbfa ("fw: rework on top of 'coap_req'")](https://github.com/golioth/golioth-zephyr-sdk/commit/a5400990dbfa4dde009601dbf3e15c15c8a08d9c)
  - [1660ba7bf840 ("samples: dfu: report FW state and reboot from main thread")](https://github.com/golioth/golioth-zephyr-sdk/commit/1660ba7bf84077fcd2fc71fbf92b381a2389445d)
  - [ef3775601f6d ("fw: change golioth_fw_report_state() API to be synchronous")](https://github.com/golioth/golioth-zephyr-sdk/commit/ef3775601f6d953f37e213ffdc61141914b9e20b)
  - [5796567cb134 ("lightdb: rework golioth_lightdb_get() on top of coap_req")](https://github.com/golioth/golioth-zephyr-sdk/commit/5796567cb1340e0539a4155c27fdc574c25cbf56)
  - [10dc6f1757de ("lightdb: rework golioth_lightdb_set() on top of coap_req")](https://github.com/golioth/golioth-zephyr-sdk/commit/10dc6f1757ded74d20547460a78cbc22ec7fee92)
  - [8f9a2fef5052 ("lightdb: rework golioth_lightdb_observe*() on top of coap_req")](https://github.com/golioth/golioth-zephyr-sdk/commit/8f9a2fef5052920f53996a27e5d69401f2758203)
  - [1d04427adddc ("lightdb: rework golioth_lightdb_delete() on top of coap_req")](https://github.com/golioth/golioth-zephyr-sdk/commit/1d04427adddce9776992fc657563247ccadb06eb)
  - [369ec2a16788 ("samples: lightdb_stream: update to new API")](https://github.com/golioth/golioth-zephyr-sdk/commit/369ec2a167885e8a03fd88743bb83add12e2229e)
- added dedicated APIs for LightDB Stream (`golioth_stream_*`), with no need to use
  `GOLIOTH_LIGHTDB_STREAM_PATH()` helper macro
- removed `GOLIOTH_LIGHTDB()` macro, which is no longer required with new LightDB APIs

### Changed
- samples wait for network interface UP and DHCP BOUND events, before returning from net_connect()
  helper function
- verified with Zephyr v3.2.0
- verified with NCS v2.1.0
- changed error reporting in Settings service to contain array with errors
- use eventfd mechanism in `system_client` for nRF91 family
- reworked internal RPC and Settings services to use shared `coap_req` (for code deduplication and
  packet retransmission)

### Removed
- CoAP message callback registration using `golioth_register_message_callback()` (no longer needed
  with CoAP agnostic APIs)
- `client->on_message()` callback (no longer needed with CoAP agnostic APIs)

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
