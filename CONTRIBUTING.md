# Contributing

Thank you for your interest in contributing to the Tenstorrent Device Access
Library (tt-dal). This document describes how to report issues and submit
changes.

## Code of Conduct

This project is governed by the [Contributor Covenant](CODE_OF_CONDUCT.md). By
participating, you are expected to uphold this code. Report unacceptable
behaviour to ospo@tenstorrent.com.

## Reporting Bugs

Report bugs through [GitHub Issues][issues]. Before opening a new issue, search
existing issues to avoid duplicates. A good report includes:

- A clear description of the problem and the expected behaviour.
- Steps to reproduce, ideally as a minimal example.
- The library version, device architecture, and kernel driver (KMD) version.

[issues]: https://github.com/tenstorrent/tt-dal/issues

Do not report security vulnerabilities through public issues. See
[SECURITY.md](SECURITY.md) for the disclosure process.

## Submitting Changes

Bug fixes and new functionality are submitted through pull requests. Pull
requests are reviewed on a weekly cadence.

1. Fork the repository and create a branch for your change.
2. Make your change, following the conventions below.
3. Ensure the test suite passes.
4. Open a pull request with a clear description of the change and its rationale.

## Conventions

- **Formatting**: C is formatted with `clang-format` and Rust with `cargo fmt`.
  Run both before submitting. Continuous integration rejects unformatted Rust
  code.
- **Commit messages**: Follow the [Conventional Commits][cc] format (for
  example, `fix(pci): correct BDF field order`).
- **Tests**: Add or update tests for any behavioural change. The C test suite
  runs under CTest. The Rust bindings are tested with `cargo test`.
- **Scope**: Keep pull requests focused. Unrelated changes belong in separate
  pull requests.

[cc]: https://www.conventionalcommits.org
