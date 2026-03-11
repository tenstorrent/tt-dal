<h1 align="center">
  <p>tt-dal</p>
</h1>

<p align="center">
  Tenstorrent Device Access Library
</p>

> [!NOTE]
>
> This library is under active development. The API is unstable and may change
> at any time before the first stable release, v1.0.0.

## About

`tt-dal` provides low-level, stateless access to Tenstorrent device hardware
through the kernel-mode driver ([KMD][tt-kmd]). It provides raw hardware
primitives through an unopinionated device model to support operations such as
discovery, memory-mapped I/O via TLB windows, ARC messaging, telemetry, and
reset operations. Higher-level libraries should build on this library for
application-level device management.

[tt-kmd]: https://github.com/tenstorrent/tt-kmd

## Design

This library is designed as a stateless C API with transparent handles and no
hidden state. Provided operations selectively use the kernel driver (KMD) or
directly interact with hardware (e.g. registers). See [DESIGN.md](/DESIGN.md)
for detailed philosophy and design decisions.

## Features

- Driver and firmware version queries
- Comprehensive error handling
- Device discovery and lifecycle management
- TLB allocations for memory-mapped I/O
- ARC controller messaging
- Telemetry snapshots
- Power state management
- Reset operations

## Usage

### Prerequisites

- [tt-kmd] kernel driver must be installed and loaded
- CMake 3.21+

### Building

```bash
# Prepare build files
cmake -B build
# Build all artifacts
make -C build all
```

The library will be built as `build/libttdal.a`.

See the public header for complete API documentation.

## Organization

The repository is organized as a pure C library. CMake is used as the build
system.

```
./
├── CMakeLists.txt   # build configuration
├── DESIGN.md        # design philosophy
├── README.md        # this document
├── ...
├── include/         # public interface
└── src/             # core implementation
    └── lib.c        # main library
```

## License

This project is proprietary, with no license granted and all rights reserved.
See [ALL-RIGHTS-RESERVED](/ALL-RIGHTS-RESERVED).

This software assists in programming Tenstorrent products. Making, using, or
selling hardware, models, or IP may require the license of rights (such as
patent rights) from Tenstorrent or others.
