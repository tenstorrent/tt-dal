fn main() {
    // Build and install libttdal via the parent CMakeLists.txt.
    let dst = cmake::Config::new("../../..").build();

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
        // Finish the builder and generate the bindings.
        .generate()
        // Unwrap the Result and panic on failure.
        .expect("unable to generate bindings")
        // Write the bindings to the $OUT_DIR/bindings.rs file.
        .write_to_file(out.join("bindings.rs"))
        .expect("unable to write bindings");

    // Compile bindgen-generated wrappers for static inline functions.
    cc::Build::new()
        .file(&extern_c)
        .include(dst.join("include"))
        .flag_if_supported("-std=c2x")
        .flag_if_supported("-Wno-implicit-function-declaration")
        .compile("ttdal_inline_wrappers");
}
