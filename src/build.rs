use std::process::Command;
use shadow_rs::BuildPattern;
use shadow_rs::ShadowBuilder;

fn main() {
    println!("cargo:rustc-link-arg-bins=target/dtn/libdtn.a");
    println!("cargo:rustc-link-arg-bins=target/dtn/isal/lib/libisal_crypto.a");
    println!("cargo:rustc-link-arg-bins=target/dtn/libnuma/lib/libnuma.a");
    println!("cargo:rustc-link-arg-bins=target/dtn/zstd/lib/libzstd.a");
    println!("cargo:rustc-link-arg-bins=-lrt");
    println!("cargo:rustc-link-arg-bins=-lc");
    println!("cargo:rustc-link-arg-bins=-no-pie");
    println!("cargo:rustc-link-arg-tests=target/dtn/libdtn.a");
    println!("cargo:rustc-link-arg-tests=target/dtn/isal/lib/libisal_crypto.a");
    println!("cargo:rustc-link-arg-tests=target/dtn/libnuma/lib/libnuma.a");
    println!("cargo:rustc-link-arg-tests=target/dtn/zstd/lib/libzstd.a");
    println!("cargo:rustc-link-arg-tests=-lrt");
    println!("cargo:rustc-link-arg-tests=-lc");
    println!("cargo:rustc-link-arg-tests=-no-pie");
     Command::new("sh").args(["./mk.sh", "argument"]).status().unwrap();
    println!("cargo:rerun-if-changed=target/dtn/libdtn.a");
    ShadowBuilder::builder()
        .build_pattern(BuildPattern::RealTime)
        .build().unwrap();
}

