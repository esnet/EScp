// use std::process::Command;

fn main() -> shadow_rs::SdResult<()>  {
    println!("cargo:rustc-link-arg-bins=build/libdtn.a");
    println!("cargo:rustc-link-arg-bins=build/isal/lib/libisal_crypto.a");
    println!("cargo:rustc-link-arg-bins=build/libnuma/lib/libnuma.a");
    println!("cargo:rustc-link-arg-bins=build/zstd/lib/libzstd.a");
    println!("cargo:rustc-link-arg-bins=-lrt");
    println!("cargo:rustc-link-arg-bins=-lc");
    println!("cargo:rustc-link-arg-bins=-no-pie");
    Command::new("sh").args(["./mk.sh", "argument"]).status().unwrap();
    println!("cargo:rerun-if-changed=build/libdtn.a");
    shadow_rs::new()
}
