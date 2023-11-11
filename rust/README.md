apt install clang hwloc-dev liburing-dev
cargo build
cargo install bindgen-cli

bindgen ../include/dtn.h -o bindings.rs --use-core  --generate-cstr


