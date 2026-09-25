<h1 align="center">
  <p>ttdal</p>
</h1>

<p align="center">
  Python bindings for the Tenstorrent Device Access Library
</p>

Idiomatic Python bindings over [tt-dal], built with [PyO3] and [maturin]. Wraps
the C library directly and exposes a submodule structure mirroring the Rust
crate.

[tt-dal]: https://github.com/tenstorrent/tt-dal
[PyO3]: https://pyo3.rs
[maturin]: https://www.maturin.rs

## Organization

The package is structured as follows:

```
./
├── Cargo.toml       # cargo manifest
├── pyproject.toml   # python package
├── README.md        # this document
├── ...
└── src/             # public bindings
```

## Installation

```bash
pip install tt-dal
```

## Building

Requires CMake 3.21+, a stable Rust toolchain, and [maturin].

```bash
maturin develop           # install into the active Python environment
maturin build --release   # build a wheel
```

## Usage

```python
import ttdal
import ttdal.tlb
import ttdal.power

# Versions
print(ttdal.__version__)    # library version
print(ttdal.kmd.version())  # kernel driver version

# Device discovery
devs = ttdal.scan()

# Open a session (context manager)
with devs[0].open() as sess:
    info = sess.info()    # static device info
    telem = sess.telemetry()
    bundle = ttdal.fw.version(sess)  # firmware bundle version

    # TLB window (context manager)
    cfg = ttdal.tlb.Config(addr=0x1000, x_end=0, y_end=0)
    with sess.alloc(ttdal.tlb.Size.Mb16, ttdal.tlb.Caching.Uncached) as tlb:
        with tlb.bind(cfg) as win:
            val = win.read_u32(0)
            win.write_u32(0, val | 1)
```
