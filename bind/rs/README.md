<h1 align="center">
  <p>ttdal</p>
</h1>

<p align="center">
  Rust bindings for the Tenstorrent Device Access Library
</p>

Safe, idiomatic Rust bindings over [tt-dal](../../README.md). The workspace is
split between `ttdal`, which exposes the safe public API, and `ttdal-sys`,
which provides the raw FFI layer.

## Organization

The workspace is structured as follows:

```
./
├── Cargo.lock       # cargo lockfile
├── Cargo.toml       # cargo manifest
├── README.md        # this document
├── ...
├── crates/          # support crates
├── src/             # public bindings
└── sys/             # raw FFI bindings
```

## Building

Requires CMake 3.21+ and a stable Rust toolchain.

```bash
cargo build
```
