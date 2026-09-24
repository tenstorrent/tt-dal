// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

use bindgen::callbacks::{IntKind, ParseCallbacks};
use semver::{Prerelease, Version};
use std::sync::Mutex;

static VERSION: Mutex<Version> = Mutex::new(Version::new(0, 0, 0));

#[derive(Debug)]
struct TtdalCallbacks;

impl ParseCallbacks for TtdalCallbacks {
    fn int_macro(&self, name: &str, value: i64) -> Option<IntKind> {
        let mut version = VERSION.lock().unwrap();
        match name {
            "TTDAL_VERSION_MAJOR" => version.major = value as u64,
            "TTDAL_VERSION_MINOR" => version.minor = value as u64,
            "TTDAL_VERSION_PATCH" => version.patch = value as u64,
            _ => {}
        }
        None
    }
}

fn main() {
    // Rebuild when the C library changes.
    //
    // bindgen's `CargoCallbacks` tracks only the installed header under
    // `OUT_DIR`, so without these the bindings go stale whenever the source
    // header is edited.
    println!("cargo:rerun-if-changed=include/ttdal.h");
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=CMakeLists.txt");

    // Build and install libttdal via the parent CMakeLists.txt.
    let dst = cmake::Config::new(env!("CARGO_MANIFEST_DIR"))
        .define("CMAKE_INSTALL_LIBDIR", "lib")
        .build();

    // Tell cargo where to find libttdal and to link it statically.
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=ttdal");

    let out = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let extern_c = out.join("extern.c");

    // The bindgen::Builder is the main entry point to bindgen, and lets you
    // build up options for the resulting bindings.
    bindgen::Builder::default()
        // The input header we would like to generate bindings for.
        .header(dst.join("include/ttdal.h").to_string_lossy())
        .clang_args([
            &format!("-I{}/include", dst.display()),
            "-fretain-comments-from-system-headers",
        ])
        .generate_comments(true)
        .prepend_enum_name(false)
        // Wrap static inline functions so they can be called from Rust.
        .wrap_static_fns(true)
        .wrap_static_fns_path(&extern_c)
        // Tell cargo to invalidate the built crate whenever any of the included
        // header files changed.
        .allowlist_file(".*ttdal\\.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .parse_callbacks(Box::new(TtdalCallbacks))
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("unable to generate bindings")
        // Write the bindings to the $OUT_DIR/bindings.rs file.
        .write_to_file(out.join("bindings.rs"))
        .expect("unable to write bindings");

    let ttdal = VERSION.lock().unwrap().clone();
    let mut cargo = Version::parse(env!("CARGO_PKG_VERSION")).unwrap();
    cargo.pre = Prerelease::EMPTY;
    if ttdal.cmp_precedence(&cargo).is_ne() {
        println!("cargo::error=version mismatch (found: {cargo}, expected: {ttdal})");
    }

    // Compile bindgen-generated wrappers for static inline functions.
    cc::Build::new()
        .file(&extern_c)
        .include(dst.join("include"))
        .flag_if_supported("-std=c2x")
        .flag_if_supported("-Wno-implicit-function-declaration")
        .compile("ttdal_inline_wrappers");
}
