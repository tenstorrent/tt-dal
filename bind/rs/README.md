<h1 align="center">
  <p>ttdal</p>
</h1>

<p align="center">
  Rust bindings for the Tenstorrent Device Access Library
</p>

Safe, idiomatic Rust bindings over [tt-dal]. They are split between `ttdal`,
which exposes the safe public API, and `ttdal-sys`, which provides the raw FFI
layer.

## Organization

The crate is structured as follows:

```text
./
├── Cargo.toml       # cargo manifest
├── README.md        # this document
├── ...
└── src/             # public bindings
```

## Installation

```bash
cargo add ttdal
```

## Building

Building requires CMake 3.21+, a C23 compiler, [libclang] for bindgen, and a
stable Rust toolchain.

```bash
cargo build
```

## Testing

```bash
cargo test
```

> [!TIP]
>
> Hardware tests are marked `#[ignore]` and require the [tt-kmd] kernel driver
> and a connected device. They must be run explicitly:
>
> ```bash
> cargo test -- --ignored           # hardware only
> cargo test -- --include-ignored   # run all tests
> ```

[tt-dal]: https://github.com/tenstorrent/tt-dal
[tt-kmd]: https://github.com/tenstorrent/tt-kmd
[libclang]: https://rust-lang.github.io/rust-bindgen/requirements.html
