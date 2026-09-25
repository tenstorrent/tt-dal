# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0]

### Build System

- *(make)* Initial file
- *(cmake)* Add fmt target
- *(cargo)* Promote workspace to repo root
- *(cmake)* Skip missing tests, examples
- *(uv)* Promote workspace to repo root
- *(pyo3)* Support v3.10 ABI
- *(cargo)* Use `libc` as workspace dependency
- *(cmake)* Initial package
- *(cargo)* Add package keys
- *(pyproject)* Add package keys
- *(cargo)* Declare MSRV
- *(cargo)* Hoist `ttdal-sys` to repo root
- *(cargo)* Allow publish
- Enforce version consistency
- *(vendor)* Bump tt-kmd from 2.7.0 to 2.11.0
- *(pyo3)* Drop `extension-module`
- *(cmake)* Pin `CMAKE_INSTALL_LIBDIR`
- *(maturin)* Drop `sdist-generator`
- *(pyproject)* Rename `{ttdal => tt-dal}`

### Continuous Integration

- *(build)* Initial impl
- *(check)* Initial impl
- *(audit)* Initial impl
- *(release)* Initial impl
- *(publish)* Initial impl

### Documentation

- Add QUIRKS.md
- *(readme)* Document stability
- *(notice)* Initial document
- *(contributing)* Initial guidelines
- *(conduct)* Initial policy
- *(security)* Initial policy
- *(readme)* Update
- *(license)* Dual-license vendored `ioctl.h`
- *(bind)* Use absolute links
- *(bind)* Add install process
- *(sys)* Initial readme

### Features

- Initial impl
- *(bind/rs)* Initial impl
- Modularize impl
- *(tlb)* [**breaking**] Rename `tt_tlb_{configure => bind}`
- *(bind/py)* Initial impl
- *(bind)* Add explicit close/free API
- Implement telemetry
- [**breaking**] Impl `tt_session_t`
- *(reset)* [**breaking**] Exclusive device access
- *(reset)* Add `tt_reset_with`
- *(err)* [**breaking**] Report failures with `errno`
- *(open)* [**breaking**] Impl `tt_open_flag_t`
- *(open)* Add `TT_OPEN_EXCL`
- *(open)* Add `TT_OPEN_NONBLOCK`
- *(bind/rs)* Persistent sessions
- *(bind/py)* Persistent sessions
- *(telem)* Trap `SIGBUS`
- [**breaking**] Adopt `[[nodiscard]]`
- *(dev)* Add `tt_reopen`
- [**breaking**] Rename version getters
- *(smc)* Initial impl

### Miscellaneous Tasks

- *(license)* All rights reserved
- *(git)* Create .gitignore
- *(git)* Simplify .gitignore
- *(rustfmt)* Initial file
- *(git)* Create .gitignore for Rust
- *(git)* Create .gitignore for Python
- *(typos)* Extend words
- *(cliff)* Initial conf
- *(deny)* Initial conf
- *(license)* Initial release
- *(license)* License understanding
- *(license)* Add SPDX headers
- Prepare for open-source release ([#1](https://github.com/tenstorrent/tt-dal/issues/1))
- *(clippy)* Apply lints
- *(cliff)* Prepare for release

### Refactor

- *(reset)* Acquire via `tt_open`
- [**breaking**] Rename `{arc => smc}`
- *(pci)* Inline `{pci => ttdal::pci}`
- [**breaking**] Rename `TT{ => DAL}_VERSION_*`
- *(bind/py)* Use `as_mut_ptr`

### Testing

- *(cargo)* Initial tests
- *(ctest)* Initial tests

[0.1.0]: https://github.com/tenstorrent/tt-dal/tree/v0.1.0

