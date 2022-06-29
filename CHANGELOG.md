# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
