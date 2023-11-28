use std::process::Command;

fn main() -> shadow_rs::SdResult<()>  {
    println!("cargo:rustc-link-arg-bins=build/libdtn.a");
    println!("cargo:rustc-link-arg-bins=build/isal/lib/libisal_crypto.a");
    println!("cargo:rustc-link-arg-bins=-lrt");
    println!("cargo:rustc-link-arg-bins=-lc");
    println!("cargo:rustc-link-arg-bins=-lnuma");
    println!("cargo:rerun-if-changed=build_libdtn.sh");
    Command::new("sh").args(["build_libdtn.sh"]).status().unwrap();
    shadow_rs::new()
}
